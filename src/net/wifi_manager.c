/**
 * Wi‑Fi STA + SoftAP captive setup portal ("Marten-Setup").
 * Connect phone → OS captive sheet → setup page (DNS hijack + HTTP catch-all).
 */
#include "net/wifi_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "spotify/player.h"

static const char *TAG = "wifi";

#define WIFI_NVS_NS "marten_wifi"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define DNS_PORT 53

static EventGroupHandle_t s_wifi_events;
static wifi_status_t s_status = WIFI_STATUS_IDLE;
static httpd_handle_t s_httpd;
static char s_ssid[33];
static int s_retry;
static TaskHandle_t s_dns_task;
static volatile bool s_dns_running;
static uint32_t s_portal_ip; /* SoftAP IPv4, network byte order */
static char s_portal_url[28]; /* e.g. http://192.168.4.1/ */

static void set_status(wifi_status_t st)
{
    s_status = st;
}

static void stop_httpd(void);
static void stop_portal_task(void *arg)
{
    (void)arg;
    stop_httpd();
    esp_wifi_set_mode(WIFI_MODE_STA);
    vTaskDelete(NULL);
}

static bool load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t sl = ssid_len;
    size_t pl = pass_len;
    esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &sl);
    esp_err_t e2 = nvs_get_str(h, "pass", pass, &pl);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && ssid[0] != '\0';
}

static bool save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    nvs_set_str(h, "ssid", ssid ? ssid : "");
    nvs_set_str(h, "pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    return true;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < 8) {
            s_retry++;
            set_status(WIFI_STATUS_CONNECTING);
            esp_wifi_connect();
            ESP_LOGI(TAG, "retry STA connect (%d)", s_retry);
        } else {
            set_status(WIFI_STATUS_FAILED);
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry = 0;
        set_status(WIFI_STATUS_CONNECTED);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        /* Teardown SoftAP off the event task (stop_httpd blocks). */
        xTaskCreate(stop_portal_task, "portal_stop", 3072, NULL, 5, NULL);
    }
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/** In-place application/x-www-form-urlencoded decode (+ and %XX). */
static void url_decode(char *s)
{
    char *src = s;
    char *dst = s;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            int hi = hex_nibble(src[1]);
            int lo = hex_nibble(src[2]);
            if (hi >= 0 && lo >= 0) {
                *dst++ = (char)((hi << 4) | lo);
                src += 3;
            } else {
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void copy_form_field(const char *body, const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    if (!body || !key || out_len == 0) {
        return;
    }
    char needle[16];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = strstr(body, needle);
    if (!p) {
        return;
    }
    p += strlen(needle);
    const char *end = strchr(p, '&');
    size_t n = end ? (size_t)(end - p) : strlen(p);
    if (n >= out_len) {
        n = out_len - 1;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    url_decode(out);
}

static const char *portal_html(void)
{
    static char page[1200];
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<meta http-equiv='Cache-Control' content='no-cache'>"
             "<title>Marten setup</title></head><body style='font-family:sans-serif;padding:1rem'>"
             "<h2>Marten setup</h2>"
             "<form method=POST action='%ssave'>"
             "<p>Home Wi‑Fi name<br><input name=ssid autocomplete=off "
             "style='width:95%%;font-size:1.2rem;padding:.4rem'></p>"
             "<p>Password<br><input name=pass type=password autocomplete=off "
             "style='width:95%%;font-size:1.2rem;padding:.4rem'></p>"
             "<p>Child playlist link or ID<br>"
             "<input name=playlist placeholder='spotify:playlist:… or open.spotify.com/…' "
             "style='width:95%%;font-size:1rem;padding:.4rem'></p>"
             "<p><button type=submit style='font-size:1.2rem;padding:.5rem 1rem'>Save</button></p>"
             "</form>"
             "<p style='color:#666;font-size:.9rem'>After Wi‑Fi connects, open Spotify on your phone "
             "and pick <b>Marten Player</b> once.</p>"
             "</body></html>",
             s_portal_url);
    return page;
}

static esp_err_t portal_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, portal_html(), HTTPD_RESP_USE_STRLEN);
}

/** Any other path (captive probes) → redirect so OS opens the sign-in sheet. */
static esp_err_t portal_captive(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", s_portal_url);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "text/html");
    /* Body helps some clients that ignore Location alone. */
    return httpd_resp_send(req, portal_html(), HTTPD_RESP_USE_STRLEN);
}

typedef struct {
    char ssid[33];
    char pass[65];
} portal_creds_t;

/** Apply creds after HTTP response is fully sent (stop_httpd mid-POST looked like a no-op). */
static void connect_after_portal_task(void *arg)
{
    portal_creds_t *creds = (portal_creds_t *)arg;
    vTaskDelay(pdMS_TO_TICKS(400)); /* let "Saved" page reach the phone */
    wifi_manager_set_credentials(creds->ssid, creds->pass);
    free(creds);
    vTaskDelete(NULL);
}

static esp_err_t portal_post(httpd_req_t *req)
{
    char body[512] = {0};
    int total = req->content_len;
    int got = 0;

    if (total > 0) {
        if (total >= (int)sizeof(body)) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body too large");
            return ESP_FAIL;
        }
        while (got < total) {
            int r = httpd_req_recv(req, body + got, total - got);
            if (r <= 0) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
                return ESP_FAIL;
            }
            got += r;
        }
    } else {
        /* Some captive WebViews omit Content-Length; read until timeout/empty. */
        while (got < (int)sizeof(body) - 1) {
            int r = httpd_req_recv(req, body + got, (int)sizeof(body) - 1 - got);
            if (r <= 0) {
                break;
            }
            got += r;
        }
    }
    body[got] = '\0';
    ESP_LOGI(TAG, "portal POST %d bytes: %.80s%s", got, body, got > 80 ? "..." : "");

    char ssid[33] = {0};
    char pass[65] = {0};
    char playlist[160] = {0};
    copy_form_field(body, "ssid", ssid, sizeof(ssid));
    copy_form_field(body, "pass", pass, sizeof(pass));
    copy_form_field(body, "playlist", playlist, sizeof(playlist));

    if (!ssid[0] && !playlist[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "need wifi or playlist");
        return ESP_FAIL;
    }

    if (playlist[0]) {
        ESP_LOGI(TAG, "portal playlist='%s'", playlist);
        player_set_curated_playlist(playlist);
    }

    if (ssid[0]) {
        ESP_LOGI(TAG, "portal saved SSID='%s'", ssid);
    }

    const char *ok =
        "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width'>"
        "<title>Marten</title></head><body style='font-family:sans-serif;padding:1rem'>"
        "<h2>Saved</h2><p>Marten is applying your settings. You can close this page.</p>"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    esp_err_t send_err = httpd_resp_send(req, ok, HTTPD_RESP_USE_STRLEN);

    if (ssid[0]) {
        portal_creds_t *creds = calloc(1, sizeof(*creds));
        if (creds) {
            strncpy(creds->ssid, ssid, sizeof(creds->ssid) - 1);
            strncpy(creds->pass, pass, sizeof(creds->pass) - 1);
            if (xTaskCreate(connect_after_portal_task, "wifi_apply", 4096, creds, 5, NULL) !=
                pdPASS) {
                free(creds);
                wifi_manager_set_credentials(ssid, pass);
            }
        } else {
            wifi_manager_set_credentials(ssid, pass);
        }
    }

    return send_err;
}

/** Minimal DNS hijack: answer every A query with SoftAP IP (hotel-style captive portal). */
static void dns_server_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        s_dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind :53 failed");
        close(sock);
        s_dns_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Captive DNS on :53 → " IPSTR, IP2STR((esp_ip4_addr_t *)&s_portal_ip));

    uint8_t buf[512];
    while (s_dns_running) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (len < 12) {
            continue;
        }

        /* Build reply in-place: copy query, set response flags, append A answer. */
        buf[2] = 0x81; /* QR=1, OPCODE=0, AA=0, TC=0, RD=1 */
        buf[3] = 0x80; /* RA=1, RCODE=0 */
        /* QDCOUNT stays; ANCOUNT=1; NSCOUNT=0; ARCOUNT=0 */
        buf[4] = 0;
        buf[5] = 1;
        buf[6] = 0;
        buf[7] = 1;
        buf[8] = 0;
        buf[9] = 0;
        buf[10] = 0;
        buf[11] = 0;

        int out = len;
        if (out + 16 > (int)sizeof(buf)) {
            continue;
        }
        /* Name pointer to question at offset 12 */
        buf[out++] = 0xC0;
        buf[out++] = 0x0C;
        buf[out++] = 0x00; /* TYPE A */
        buf[out++] = 0x01;
        buf[out++] = 0x00; /* CLASS IN */
        buf[out++] = 0x01;
        buf[out++] = 0x00; /* TTL 60s */
        buf[out++] = 0x00;
        buf[out++] = 0x00;
        buf[out++] = 0x3C;
        buf[out++] = 0x00; /* RDLENGTH 4 */
        buf[out++] = 0x04;
        memcpy(buf + out, &s_portal_ip, 4);
        out += 4;

        sendto(sock, buf, out, 0, (struct sockaddr *)&from, fromlen);
    }

    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void stop_dns(void)
{
    if (!s_dns_running && !s_dns_task) {
        return;
    }
    s_dns_running = false;
    for (int i = 0; i < 30 && s_dns_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void start_dns(void)
{
    stop_dns();
    s_dns_running = true;
    if (xTaskCreate(dns_server_task, "captive_dns", 3072, NULL, 5, &s_dns_task) != pdPASS) {
        s_dns_running = false;
        s_dns_task = NULL;
        ESP_LOGE(TAG, "DNS task create failed");
    }
}

static void stop_httpd(void)
{
    stop_dns();
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
}

static bool start_httpd(void)
{
    stop_httpd();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        return false;
    }
    httpd_uri_t get = {.uri = "/", .method = HTTP_GET, .handler = portal_get};
    httpd_uri_t post = {.uri = "/save", .method = HTTP_POST, .handler = portal_post};
    httpd_uri_t captive = {.uri = "/*", .method = HTTP_GET, .handler = portal_captive};
    httpd_register_uri_handler(s_httpd, &get);
    httpd_register_uri_handler(s_httpd, &post);
    httpd_register_uri_handler(s_httpd, &captive);
    start_dns();
    return true;
}

bool wifi_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    return true;
}

void wifi_manager_start_portal(void)
{
    stop_httpd();
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, "Marten-Setup", sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen("Marten-Setup");
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* SoftAP DHCP already advertises us as DNS; resolve SoftAP IP for hijack replies. */
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info = {0};
    if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        s_portal_ip = ip_info.ip.addr;
    } else {
        s_portal_ip = ipaddr_addr("192.168.4.1");
    }
    snprintf(s_portal_url, sizeof(s_portal_url), "http://" IPSTR "/",
             IP2STR((esp_ip4_addr_t *)&s_portal_ip));

    start_httpd();
    set_status(WIFI_STATUS_AP_PORTAL);
    ESP_LOGI(TAG, "Captive portal 'Marten-Setup' — %s", s_portal_url);
}

static bool try_sta(const char *ssid, const char *pass, int timeout_ms)
{
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);

    s_retry = 0;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    set_status(WIFI_STATUS_CONNECTING);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Disable modem sleep so Spotify mDNS stays reliable */
    esp_wifi_set_ps(WIFI_PS_NONE);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE,
                                           pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_manager_start(void)
{
    char ssid[33] = {0};
    char pass[65] = {0};
    if (load_creds(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Connecting to saved SSID '%s'", ssid);
        if (try_sta(ssid, pass, 20000)) {
            stop_httpd();
            return;
        }
        ESP_LOGW(TAG, "Saved Wi‑Fi failed — starting setup portal");
    } else {
        ESP_LOGI(TAG, "No Wi‑Fi credentials — starting setup portal");
    }
    wifi_manager_start_portal();
}

bool wifi_manager_set_credentials(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0]) {
        return false;
    }
    if (!save_creds(ssid, password ? password : "")) {
        return false;
    }

    /* Stay APSTA so SoftAP (and success page) stays up until STA gets an IP. */
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, password ? password : "", sizeof(sta.sta.password));
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_retry = 0;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    set_status(WIFI_STATUS_CONNECTING);

    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_start();
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_disconnect();
    esp_wifi_connect();
    ESP_LOGI(TAG, "STA connecting to '%s'", ssid);
    return true;
}

bool wifi_manager_is_connected(void)
{
    return s_status == WIFI_STATUS_CONNECTED;
}

wifi_status_t wifi_manager_get_status(void)
{
    return s_status;
}

void wifi_manager_get_ssid(char *buf, size_t buflen)
{
    if (!buf || buflen == 0) {
        return;
    }
    strncpy(buf, s_ssid, buflen - 1);
    buf[buflen - 1] = '\0';
}
