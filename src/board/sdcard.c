/**
 * Waveshare 1.85 TF slot — SDMMC 1-bit (CLK/CMD/D0) + EXIO3 as CS/D3 pull-up.
 */
#include "board/sdcard.h"
#include "board_pins.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "TCA9554PWR.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sdcard";
#define SD_MOUNT "/sdcard"
#define COVER_DIR SD_MOUNT "/marten/covers"
#define COVER_MAGIC "MCV1"

static bool s_mounted;
static sdmmc_card_t *s_card;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint16_t w;
    uint16_t h;
    uint32_t hash;
} cover_hdr_t;

static uint32_t url_hash(const char *url)
{
    uint32_t h = 2166136261u;
    if (!url) {
        return h;
    }
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        h ^= *p;
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static void cover_path(uint32_t hash, char *out, size_t out_len)
{
    snprintf(out, out_len, COVER_DIR "/%08lx.rgb", (unsigned long)hash);
}

bool board_sd_is_mounted(void)
{
    return s_mounted;
}

bool board_sd_init(void)
{
    if (s_mounted) {
        return true;
    }

    /* D3/CS lives on the expander — hold high for 1-bit SDMMC. */
    Mode_EXIO(TCA9554_EXIO3, 0);
    Set_EXIO(TCA9554_EXIO3, true);
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot.clk = PIN_SD_SCK;
    slot.cmd = PIN_SD_MOSI;
    slot.d0 = PIN_SD_MISO;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
#endif
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err =
        esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TF mount failed: %s (format FAT32?)", esp_err_to_name(err));
        return false;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);

    mkdir(SD_MOUNT "/marten", 0755);
    mkdir(COVER_DIR, 0755);
    ESP_LOGI(TAG, "TF mounted at %s", SD_MOUNT);
    return true;
}

bool board_sd_cover_load(const char *url, uint8_t **pixels, int *w, int *h)
{
    if (!s_mounted || !url || !url[0] || !pixels || !w || !h) {
        return false;
    }
    *pixels = NULL;
    char path[96];
    uint32_t hash = url_hash(url);
    cover_path(hash, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    cover_hdr_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr.magic, COVER_MAGIC, 4) != 0 || hdr.hash != hash || hdr.w < 8 ||
        hdr.h < 8 || hdr.w > 512 || hdr.h > 512) {
        fclose(f);
        return false;
    }
    size_t bytes = (size_t)hdr.w * (size_t)hdr.h * 2;
    uint8_t *buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        return false;
    }
    if (fread(buf, 1, bytes, f) != bytes) {
        heap_caps_free(buf);
        fclose(f);
        return false;
    }
    fclose(f);
    *pixels = buf;
    *w = hdr.w;
    *h = hdr.h;
    ESP_LOGI(TAG, "cover hit %dx%d %.40s", hdr.w, hdr.h, url);
    return true;
}

bool board_sd_cover_save(const char *url, const uint8_t *pixels, int w, int h)
{
    if (!s_mounted || !url || !url[0] || !pixels || w < 8 || h < 8) {
        return false;
    }
    char path[96];
    uint32_t hash = url_hash(url);
    cover_path(hash, path, sizeof(path));

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "cover save open failed %s", path);
        return false;
    }
    cover_hdr_t hdr = {
        .magic = {'M', 'C', 'V', '1'},
        .w = (uint16_t)w,
        .h = (uint16_t)h,
        .hash = hash,
    };
    size_t bytes = (size_t)w * (size_t)h * 2;
    bool ok = fwrite(&hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    if (ok) {
        size_t off = 0;
        while (off < bytes) {
            size_t chunk = bytes - off;
            if (chunk > 16 * 1024) {
                chunk = 16 * 1024;
            }
            if (fwrite(pixels + off, 1, chunk, f) != chunk) {
                ok = false;
                break;
            }
            off += chunk;
            vTaskDelay(1);
        }
    }
    fclose(f);
    if (ok) {
        ESP_LOGI(TAG, "cover saved %dx%d (%u KB)", w, h, (unsigned)(bytes / 1024));
    } else {
        remove(path);
        ESP_LOGW(TAG, "cover save write failed");
    }
    return ok;
}
