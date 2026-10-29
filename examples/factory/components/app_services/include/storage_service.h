/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sd_protocol_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_SD_ROOT          "/sdcard"
#define STORAGE_DIR_MUSIC        STORAGE_SD_ROOT "/music"
#define STORAGE_DIR_VIDEOS       STORAGE_SD_ROOT "/videos"
#define STORAGE_DIR_PICTURES     STORAGE_SD_ROOT "/pictures"
#define STORAGE_DIR_BOOKS        STORAGE_SD_ROOT "/books"
#define STORAGE_DIR_FONTS        STORAGE_SD_ROOT "/fonts"

typedef enum {
    STORAGE_SD_NO_CARD,      /*!< No card answered, or it was never probed */
    STORAGE_SD_MOUNTED,      /*!< FAT filesystem is available to applications */
    STORAGE_SD_USB,          /*!< Card is exported to a USB host; applications must not touch it */
    STORAGE_SD_UNFORMATTED,  /*!< Card answered but carries no FAT filesystem */
    STORAGE_SD_ERROR,        /*!< Card answered but could not be mounted */
} storage_sd_state_t;

typedef enum {
    STORAGE_EVENT_SD_RELEASE,  /*!< Close every file under STORAGE_SD_ROOT before returning */
    STORAGE_EVENT_SD_MOUNTED,  /*!< The card is mounted again */
} storage_event_t;

/**
 * @brief  Storage event callback
 *
 *         STORAGE_EVENT_SD_RELEASE is delivered synchronously, normally from the
 *         LVGL task, before the filesystem is unmounted.
 */
typedef void (*storage_event_cb_t)(storage_event_t event, void *user_ctx);

/**
 * @brief  Probe and mount the microSD card and create the media directories
 *
 *         A missing or unreadable card is reported through storage_sd_get_state()
 *         and does not fail initialization. Must run after the wireless link is
 *         up, because that link enables the SDMMC IO supply.
 */
esp_err_t storage_service_init(void);

storage_sd_state_t storage_sd_get_state(void);
const char *storage_sd_state_name(storage_sd_state_t state);
bool storage_sd_is_mounted(void);
esp_err_t storage_sd_get_usage(uint64_t *total_bytes, uint64_t *free_bytes);

/**
 * @brief  Probe the card again, for example after a card was inserted
 */
esp_err_t storage_sd_remount(void);

esp_err_t storage_add_listener(storage_event_cb_t cb, void *user_ctx);
esp_err_t storage_remove_listener(storage_event_cb_t cb, void *user_ctx);

/**
 * @brief  Hand the card to a USB host
 *
 *         Delivers STORAGE_EVENT_SD_RELEASE, unmounts the filesystem and keeps the
 *         card initialized so raw sectors can be served over USB.
 */
esp_err_t storage_sd_export_begin(sdmmc_card_t **ret_card);

/**
 * @brief  Take the card back from the USB host, remount it and deliver STORAGE_EVENT_SD_MOUNTED
 */
esp_err_t storage_sd_export_end(void);

#ifdef __cplusplus
}
#endif
