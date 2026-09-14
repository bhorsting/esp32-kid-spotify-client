#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATUS_IDLE = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED,
    WIFI_STATUS_AP_PORTAL,
} wifi_status_t;

/** Init NVS + netif + wifi driver. Call once from app_main. */
bool wifi_manager_init(void);

/** Try STA from NVS; if missing/fail, start SoftAP portal "Marten-Setup". */
void wifi_manager_start(void);

/** Start / restart SoftAP captive setup portal (auto-opens on phone like hotel Wi‑Fi). */
void wifi_manager_start_portal(void);

bool wifi_manager_is_connected(void);
wifi_status_t wifi_manager_get_status(void);

/** Copy current SSID (STA) into buf; empty if unknown. */
void wifi_manager_get_ssid(char *buf, size_t buflen);

/** Save credentials to NVS and attempt STA reconnect. */
bool wifi_manager_set_credentials(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif
