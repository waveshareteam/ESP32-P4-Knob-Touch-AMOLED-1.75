/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "sdmmc_cmd.h"
#include "tusb.h"
#include "storage_service.h"
#include "usb_service.h"

#define USB_RHPORT_HS       1
#define USB_TASK_STACK      4096
#define USB_TASK_PRIORITY   5

static const char *TAG = "usb_service";

static usb_phy_handle_t s_phy;
static bool s_stack_ready;
static usb_service_mode_t s_mode = USB_SERVICE_MODE_OFF;

/* Guards s_card against usb_service_stop() while the USB task serves a sector request. */
static SemaphoreHandle_t s_card_lock;
static sdmmc_card_t *s_card;
static volatile bool s_ejected;

static void usb_device_task(void *arg)
{
    (void)arg;
    for (;;) {
        tud_task();
    }
}

static esp_err_t usb_stack_start(void)
{
    if (s_stack_ready) {
        return tud_connect() ? ESP_OK : ESP_FAIL;
    }

    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_UTMI,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_HIGH,
    };
    ESP_RETURN_ON_ERROR(usb_new_phy(&phy_config, &s_phy), TAG, "USB PHY init failed");

    const tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_HIGH,
    };
    ESP_RETURN_ON_FALSE(tusb_init(USB_RHPORT_HS, &dev_init), ESP_FAIL, TAG, "TinyUSB init failed");
    ESP_RETURN_ON_FALSE(xTaskCreate(usb_device_task, "usb_device", USB_TASK_STACK, NULL, USB_TASK_PRIORITY, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "Create USB task failed");
    s_stack_ready = true;
    return ESP_OK;
}

esp_err_t usb_service_start_sd_drive(void)
{
    ESP_RETURN_ON_FALSE(s_mode == USB_SERVICE_MODE_OFF, ESP_ERR_INVALID_STATE, TAG, "USB is busy");
    if (!s_card_lock) {
        s_card_lock = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_card_lock, ESP_ERR_NO_MEM, TAG, "Create lock failed");
    }

    sdmmc_card_t *card = NULL;
    ESP_RETURN_ON_ERROR(storage_sd_export_begin(&card), TAG, "microSD is not available");

    xSemaphoreTake(s_card_lock, portMAX_DELAY);
    s_card = card;
    s_ejected = false;
    xSemaphoreGive(s_card_lock);
    s_mode = USB_SERVICE_MODE_SD_DRIVE;

    esp_err_t ret = usb_stack_start();
    if (ret != ESP_OK) {
        usb_service_stop();
        return ret;
    }
    ESP_LOGI(TAG, "microSD drive attached");
    return ESP_OK;
}

esp_err_t usb_service_start_display(void)
{
    ESP_RETURN_ON_FALSE(s_mode == USB_SERVICE_MODE_OFF, ESP_ERR_INVALID_STATE, TAG, "USB is busy");
    // The descriptors follow the mode, so set it before the host enumerates
    s_mode = USB_SERVICE_MODE_DISPLAY;
    esp_err_t ret = usb_stack_start();
    if (ret != ESP_OK) {
        s_mode = USB_SERVICE_MODE_OFF;
        return ret;
    }
    ESP_LOGI(TAG, "Extended screen attached");
    return ESP_OK;
}

esp_err_t usb_service_stop(void)
{
    if (s_mode == USB_SERVICE_MODE_OFF) {
        return ESP_OK;
    }
    if (s_stack_ready) {
        tud_disconnect();
    }
    if (s_mode == USB_SERVICE_MODE_DISPLAY) {
        s_mode = USB_SERVICE_MODE_OFF;
        ESP_LOGI(TAG, "USB detached");
        return ESP_OK;
    }

    // Wait for an in-flight sector transfer, then stop serving the card.
    xSemaphoreTake(s_card_lock, portMAX_DELAY);
    s_card = NULL;
    xSemaphoreGive(s_card_lock);
    s_mode = USB_SERVICE_MODE_OFF;

    ESP_LOGI(TAG, "USB detached");
    return storage_sd_export_end();
}

usb_service_mode_t usb_service_get_mode(void)
{
    return s_mode;
}

usb_drive_status_t usb_service_get_drive_status(void)
{
    if (s_mode != USB_SERVICE_MODE_SD_DRIVE) {
        return USB_DRIVE_STATUS_OFF;
    }
    if (s_ejected) {
        return USB_DRIVE_STATUS_EJECTED;
    }
    return tud_mounted() ? USB_DRIVE_STATUS_CONNECTED : USB_DRIVE_STATUS_WAITING_HOST;
}

/* ---------------------------------------------------------------------------
 * TinyUSB mass-storage callbacks, called from usb_device_task
 * ------------------------------------------------------------------------- */

static void copy_padded(uint8_t *dst, size_t size, const char *src)
{
    memset(dst, ' ', size);
    memcpy(dst, src, strnlen(src, size));
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    copy_padded(vendor_id, 8, "Wavesha");
    copy_padded(product_id, 16, "P4 Knob microSD");
    copy_padded(product_rev, 4, "1.0");
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (s_card && !s_ejected) {
        return true;
    }
    // Medium not present
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    sdmmc_card_t *card = s_card;
    *block_count = card ? card->csd.capacity : 0;
    *block_size = card ? card->csd.sector_size : 512;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    if (load_eject) {
        s_ejected = !start;
        ESP_LOGI(TAG, "Host %s the drive", start ? "loaded" : "ejected");
    }
    return true;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

static int32_t transfer_sectors(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize, bool write)
{
    xSemaphoreTake(s_card_lock, portMAX_DELAY);
    sdmmc_card_t *card = s_card;
    int32_t ret = -1;
    if (card && offset == 0 && bufsize % card->csd.sector_size == 0) {
        size_t count = bufsize / card->csd.sector_size;
        esp_err_t err = write ? sdmmc_write_sectors(card, buffer, lba, count) : sdmmc_read_sectors(card, buffer, lba, count);
        if (err == ESP_OK) {
            ret = (int32_t)bufsize;
        } else {
            ESP_LOGE(TAG, "%s LBA %" PRIu32 " x%u failed: %s", write ? "Write" : "Read", lba, (unsigned)count,
                     esp_err_to_name(err));
        }
    }
    xSemaphoreGive(s_card_lock);

    if (ret < 0) {
        // Unrecovered read error / write error
        tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, write ? 0x0C : 0x11, 0x00);
    }
    return ret;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    return transfer_sectors(lun, lba, offset, buffer, bufsize, false);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    return transfer_sectors(lun, lba, offset, buffer, bufsize, true);
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;
    switch (scsi_cmd[0]) {
    case 0x35:  // SYNCHRONIZE CACHE (10): writes reach the card before the callback returns
        return 0;
    default:
        // Invalid command operation code
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "Host configured the device");
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "Host released the device");
}
