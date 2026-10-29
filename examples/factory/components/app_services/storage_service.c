/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "esp_board_manager.h"
#include "dev_fs_fat.h"
#include "storage_service.h"

#define BOARD_DEV_FS_SDCARD  "fs_sdcard"
#define MAX_LISTENERS        8
#define NO_DRIVE             0xFF
#define NVS_NAMESPACE        "storage"
#define NVS_KEY_BUS_WIDTH    "sd_bus_width"
#define SECTOR_ALIGN         64

typedef struct {
    storage_event_cb_t cb;
    void *user_ctx;
} listener_t;

/* Bus width that passed the write test for the last card seen, keyed by its CID. */
typedef struct {
    uint32_t cid_crc;
    uint8_t bus_width;
} bus_width_record_t;

static const char *TAG = "storage";

static const char *const s_media_dirs[] = {
    STORAGE_DIR_MUSIC,
    STORAGE_DIR_VIDEOS,
    STORAGE_DIR_PICTURES,
    STORAGE_DIR_BOOKS,
    STORAGE_DIR_FONTS,
};

static SemaphoreHandle_t s_lock;
static storage_sd_state_t s_state = STORAGE_SD_NO_CARD;
static const dev_fs_fat_config_t *s_cfg;
static sdmmc_host_t s_host;
static sdmmc_card_t *s_card;
/* Reuse an aligned buffer for transfers that cannot use the application buffer directly.
 * ESP-IDF v6.1 defaults to chunks of 16 sectors; this buffer allows up to 64 sectors per
 * command without allocating a temporary DMA buffer for each transfer. */
#define UNALIGNED_CHUNK_SECTORS 64
static void *s_dma_bounce;
static sd_pwr_ctrl_handle_t s_pwr_ctrl;
static uint8_t s_bus_width;
static BYTE s_pdrv = NO_DRIVE;
static listener_t s_listeners[MAX_LISTENERS];

static void card_deinit_locked(void)
{
    if (!s_card) {
        return;
    }
    // Only this slot is removed; the SDIO wireless link keeps the shared controller.
    sdmmc_host_deinit_slot(s_host.slot);
    free(s_card);
    s_card = NULL;
    if (s_pwr_ctrl) {
        sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl);
        s_pwr_ctrl = NULL;
    }
}

static esp_err_t card_init_locked(uint8_t bus_width)
{
    const dev_fs_fat_sdmmc_sub_config_t *sub = &s_cfg->sub_cfg.sdmmc;

    s_host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    s_host.slot = sub->slot;
    s_host.max_freq_khz = s_cfg->frequency;
    if (!s_dma_bounce) {
        s_dma_bounce = heap_caps_aligned_calloc(128, 1, UNALIGNED_CHUNK_SECTORS * 512,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    }
    if (s_dma_bounce) {
        s_host.unaligned_multi_block_rw_max_chunk_size = UNALIGNED_CHUNK_SECTORS;
        s_host.dma_aligned_buffer = s_dma_bounce;
    }
    if (bus_width == 1) {
        s_host.flags &= ~(SDMMC_HOST_FLAG_4BIT | SDMMC_HOST_FLAG_8BIT | SDMMC_HOST_FLAG_DDR);
    }

    if (sub->ldo_chan_id >= 0) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = sub->ldo_chan_id,
        };
        ESP_RETURN_ON_ERROR(sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl), TAG, "Create SD LDO failed");
        s_host.pwr_ctrl_handle = s_pwr_ctrl;
    }

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = sub->pins.clk;
    slot_config.cmd = sub->pins.cmd;
    slot_config.d0 = sub->pins.d0;
    slot_config.d1 = sub->pins.d1;
    slot_config.d2 = sub->pins.d2;
    slot_config.d3 = sub->pins.d3;
    slot_config.cd = sub->pins.cd;
    slot_config.wp = sub->pins.wp;
    slot_config.width = bus_width;
    slot_config.flags = sub->slot_flags;

    esp_err_t ret = sdmmc_host_init();  // Returns ESP_OK when the wireless link already created the host
    if (ret == ESP_OK) {
        ret = sdmmc_host_init_slot(s_host.slot, &slot_config);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC slot %d init failed: %s", s_host.slot, esp_err_to_name(ret));
        if (s_pwr_ctrl) {
            sd_pwr_ctrl_del_on_chip_ldo(s_pwr_ctrl);
            s_pwr_ctrl = NULL;
        }
        return ret;
    }

    s_card = calloc(1, sizeof(sdmmc_card_t));
    if (!s_card) {
        sdmmc_host_deinit_slot(s_host.slot);
        return ESP_ERR_NO_MEM;
    }
    ret = sdmmc_card_init(&s_host, s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No usable microSD card: %s", esp_err_to_name(ret));
        card_deinit_locked();
        return ret;
    }
    s_bus_width = bus_width;
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

static void fat_unmount_locked(void)
{
    if (s_pdrv == NO_DRIVE) {
        return;
    }
    char drv[3] = {(char)('0' + s_pdrv), ':', 0};
    f_mount(NULL, drv, 0);
    esp_vfs_fat_unregister_path(STORAGE_SD_ROOT);
    ff_diskio_unregister(s_pdrv);
    s_pdrv = NO_DRIVE;
}

static storage_sd_state_t fat_mount_locked(void)
{
    BYTE pdrv = NO_DRIVE;
    if (ff_diskio_get_drive(&pdrv) != ESP_OK || pdrv == NO_DRIVE) {
        ESP_LOGE(TAG, "No free FATFS drive");
        return STORAGE_SD_ERROR;
    }
    ff_diskio_register_sdmmc(pdrv, s_card);

    char drv[3] = {(char)('0' + pdrv), ':', 0};
    const esp_vfs_fat_conf_t conf = {
        .base_path = STORAGE_SD_ROOT,
        .fat_drive = drv,
        .max_files = s_cfg->vfs_config.max_files,
    };
    FATFS *fs = NULL;
    esp_err_t err = esp_vfs_fat_register(&conf, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Register FATFS failed: %s", esp_err_to_name(err));
        ff_diskio_unregister(pdrv);
        return STORAGE_SD_ERROR;
    }

    FRESULT res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        ESP_LOGW(TAG, "Mount FAT failed (%d)", res);
        esp_vfs_fat_unregister_path(STORAGE_SD_ROOT);
        ff_diskio_unregister(pdrv);
        return res == FR_NO_FILESYSTEM ? STORAGE_SD_UNFORMATTED : STORAGE_SD_ERROR;
    }
    s_pdrv = pdrv;
    return STORAGE_SD_MOUNTED;
}

/* Creates the media directories applications expect. */
static void prepare_card_locked(void)
{
    for (size_t i = 0; i < sizeof(s_media_dirs) / sizeof(s_media_dirs[0]); i++) {
        if (mkdir(s_media_dirs[i], 0775) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "Create %s failed: errno %d", s_media_dirs[i], errno);
        }
    }
}

static uint32_t card_cid_crc(const sdmmc_card_t *card)
{
    return esp_rom_crc32_le(0, (const uint8_t *)&card->cid, sizeof(card->cid));
}

static bool load_bus_width(bus_width_record_t *record)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*record);
    bool found = nvs_get_blob(nvs, NVS_KEY_BUS_WIDTH, record, &size) == ESP_OK && size == sizeof(*record) &&
                 (record->bus_width == 1 || record->bus_width == 4);
    nvs_close(nvs);
    return found;
}

static void save_bus_width(uint32_t cid_crc, uint8_t bus_width)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
        return;
    }
    const bus_width_record_t record = {
        .cid_crc = cid_crc,
        .bus_width = bus_width,
    };
    if (nvs_set_blob(nvs, NVS_KEY_BUS_WIDTH, &record, sizeof(record)) == ESP_OK) {
        nvs_commit(nvs);
    }
    nvs_close(nvs);
}

/* Returns a sector between the MBR and the first partition, which no filesystem
 * uses, or 0 when the card has no partition table (FAT starts at sector 0). */
static uint32_t find_spare_sector_locked(uint8_t *buf)
{
    if (sdmmc_read_sectors(s_card, buf, 0, 1) != ESP_OK || buf[510] != 0x55 || buf[511] != 0xAA ||
            buf[0] == 0xEB || buf[0] == 0xE9) {
        return 0;
    }
    uint32_t first_start = UINT32_MAX;
    for (int i = 0; i < 4; i++) {
        const uint8_t *entry = &buf[446 + i * 16];
        if (entry[4] == 0 || (entry[0] != 0x00 && entry[0] != 0x80)) {
            continue;
        }
        uint32_t start = entry[8] | (entry[9] << 8) | (entry[10] << 16) | ((uint32_t)entry[11] << 24);
        if (start > 0 && start < first_start) {
            first_start = start;
        }
    }
    return (first_start != UINT32_MAX && first_start > 1) ? first_start - 1 : 0;
}

/* Writes a pattern to the spare sector and reads it back. The original content is
 * kept in `orig` so it can be restored even if the bus width has to change. */
static bool spare_sector_write_ok_locked(uint32_t sector, uint8_t *orig, uint8_t *work)
{
    const size_t size = s_card->csd.sector_size;
    for (size_t i = 0; i < size; i++) {
        work[i] = (uint8_t)(i * 7 + 3);
    }
    bool ok = sdmmc_write_sectors(s_card, work, sector, 1) == ESP_OK;
    if (ok) {
        memset(work, 0, size);
        ok = sdmmc_read_sectors(s_card, work, sector, 1) == ESP_OK;
        for (size_t i = 0; ok && i < size; i++) {
            ok = work[i] == (uint8_t)(i * 7 + 3);
        }
    }
    if (ok) {
        ok = sdmmc_write_sectors(s_card, orig, sector, 1) == ESP_OK;
    }
    return ok;
}

/* Picks the widest bus that writes reliably. The card was started at the width
 * stored for the last card seen; a different card is tested once, on a sector
 * outside any filesystem, at the configured width. */
static esp_err_t select_bus_width_locked(uint8_t configured, const bus_width_record_t *known)
{
    if (configured == 1) {
        return ESP_OK;
    }
    const uint32_t cid_crc = card_cid_crc(s_card);
    if (known && known->cid_crc == cid_crc) {
        if (s_bus_width != configured) {
            ESP_LOGI(TAG, "Using %u-bit for this card, as tested before", s_bus_width);
        }
        return ESP_OK;
    }
    if (s_bus_width != configured) {
        card_deinit_locked();
        ESP_RETURN_ON_ERROR(card_init_locked(configured), TAG, "Init at %u-bit failed", configured);
    }

    const size_t size = s_card->csd.sector_size;
    uint8_t *orig = heap_caps_aligned_alloc(SECTOR_ALIGN, size, MALLOC_CAP_DMA);
    uint8_t *work = heap_caps_aligned_alloc(SECTOR_ALIGN, size, MALLOC_CAP_DMA);
    if (!orig || !work) {
        free(orig);
        free(work);
        return ESP_ERR_NO_MEM;
    }

    const uint32_t spare = find_spare_sector_locked(work);
    const bool have_orig = spare != 0 && sdmmc_read_sectors(s_card, orig, spare, 1) == ESP_OK;
    bool ok = false;
    if (!have_orig) {
        ESP_LOGW(TAG, "No spare sector to test %u-bit writes; using 1-bit", configured);
    } else if (!(ok = spare_sector_write_ok_locked(spare, orig, work))) {
        ESP_LOGW(TAG, "%u-bit writes fail on this card; using 1-bit", configured);
    }

    esp_err_t ret = ESP_OK;
    const uint8_t width = ok ? configured : 1;
    if (!ok) {
        card_deinit_locked();
        ret = card_init_locked(width);
        if (ret == ESP_OK && have_orig) {
            sdmmc_write_sectors(s_card, orig, spare, 1);
        }
    }
    free(orig);
    free(work);
    if (ret == ESP_OK) {
        save_bus_width(cid_crc, width);
    }
    return ret;
}

static esp_err_t probe_and_mount_locked(void)
{
    const uint8_t configured = s_cfg->sub_cfg.sdmmc.bus_width;
    bus_width_record_t known;
    const bool have_known = configured > 1 && load_bus_width(&known);
    if (card_init_locked(have_known ? known.bus_width : configured) != ESP_OK ||
            select_bus_width_locked(configured, have_known ? &known : NULL) != ESP_OK) {
        card_deinit_locked();
        s_state = STORAGE_SD_NO_CARD;
        return ESP_ERR_NOT_FOUND;
    }
    s_state = fat_mount_locked();
    if (s_state != STORAGE_SD_MOUNTED) {
        // Keep the card initialized so it can still be exported and formatted over USB.
        return ESP_FAIL;
    }
    prepare_card_locked();
    ESP_LOGI(TAG, "microSD ready at %s (%u-bit)", STORAGE_SD_ROOT, s_bus_width);
    return ESP_OK;
}

static void notify(storage_event_t event)
{
    listener_t snapshot[MAX_LISTENERS];
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memcpy(snapshot, s_listeners, sizeof(snapshot));
    xSemaphoreGive(s_lock);

    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (snapshot[i].cb) {
            snapshot[i].cb(event, snapshot[i].user_ctx);
        }
    }
}

esp_err_t storage_service_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "Create lock failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(BOARD_DEV_FS_SDCARD, (void **)&s_cfg), TAG,
                        "fs_sdcard config missing");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    probe_and_mount_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "microSD state: %s", storage_sd_state_name(s_state));
    return ESP_OK;
}

storage_sd_state_t storage_sd_get_state(void)
{
    return s_state;
}

const char *storage_sd_state_name(storage_sd_state_t state)
{
    switch (state) {
    case STORAGE_SD_NO_CARD:
        return "no card";
    case STORAGE_SD_MOUNTED:
        return "mounted";
    case STORAGE_SD_USB:
        return "exported to USB";
    case STORAGE_SD_UNFORMATTED:
        return "not formatted";
    case STORAGE_SD_ERROR:
    default:
        return "error";
    }
}

bool storage_sd_is_mounted(void)
{
    return s_state == STORAGE_SD_MOUNTED;
}

esp_err_t storage_sd_get_usage(uint64_t *total_bytes, uint64_t *free_bytes)
{
    ESP_RETURN_ON_FALSE(total_bytes && free_bytes, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t ret = ESP_ERR_INVALID_STATE;
    if (s_state == STORAGE_SD_MOUNTED) {
        ret = esp_vfs_fat_info(STORAGE_SD_ROOT, total_bytes, free_bytes);
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t storage_sd_remount(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    ESP_RETURN_ON_FALSE(s_state != STORAGE_SD_USB, ESP_ERR_INVALID_STATE, TAG, "Card is exported to USB");

    if (s_state == STORAGE_SD_MOUNTED) {
        notify(STORAGE_EVENT_SD_RELEASE);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fat_unmount_locked();
    card_deinit_locked();
    esp_err_t ret = probe_and_mount_locked();
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "microSD state: %s", storage_sd_state_name(s_state));

    if (s_state == STORAGE_SD_MOUNTED) {
        notify(STORAGE_EVENT_SD_MOUNTED);
    }
    return ret;
}

esp_err_t storage_add_listener(storage_event_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(cb, ESP_ERR_INVALID_ARG, TAG, "Invalid callback");
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    esp_err_t ret = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!s_listeners[i].cb) {
            s_listeners[i] = (listener_t) {
                .cb = cb, .user_ctx = user_ctx
            };
            ret = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return ret;
}

esp_err_t storage_remove_listener(storage_event_cb_t cb, void *user_ctx)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (s_listeners[i].cb == cb && s_listeners[i].user_ctx == user_ctx) {
            s_listeners[i] = (listener_t) {
                0
            };
        }
    }
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t storage_sd_export_begin(sdmmc_card_t **ret_card)
{
    ESP_RETURN_ON_FALSE(ret_card, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments");
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    ESP_RETURN_ON_FALSE(s_card && s_state != STORAGE_SD_USB, ESP_ERR_INVALID_STATE, TAG, "No card to export");

    if (s_state == STORAGE_SD_MOUNTED) {
        notify(STORAGE_EVENT_SD_RELEASE);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    fat_unmount_locked();
    s_state = STORAGE_SD_USB;
    *ret_card = s_card;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "microSD handed to USB");
    return ESP_OK;
}

esp_err_t storage_sd_export_end(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "Service not initialized");
    ESP_RETURN_ON_FALSE(s_state == STORAGE_SD_USB, ESP_ERR_INVALID_STATE, TAG, "Card is not exported");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    // The host may have reformatted or repartitioned the card, so mount from scratch.
    s_state = fat_mount_locked();
    if (s_state == STORAGE_SD_MOUNTED) {
        prepare_card_locked();
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "microSD back from USB: %s", storage_sd_state_name(s_state));

    if (s_state == STORAGE_SD_MOUNTED) {
        notify(STORAGE_EVENT_SD_MOUNTED);
    }
    return s_state == STORAGE_SD_MOUNTED ? ESP_OK : ESP_FAIL;
}
