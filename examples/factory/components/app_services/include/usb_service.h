/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The USB 2.0 high-speed port is wired to the adapter board's Type-C
 * connector in the orientation opposite to the USB-serial bridge, so the
 * host only sees this device after the plug is flipped.
 *
 * The port serves one function at a time; starting one while another runs
 * fails with ESP_ERR_INVALID_STATE.
 */
typedef enum {
    USB_SERVICE_MODE_OFF,       /*!< Device is detached from the bus */
    USB_SERVICE_MODE_SD_DRIVE,  /*!< microSD card is exposed as a mass-storage drive */
    USB_SERVICE_MODE_DISPLAY,   /*!< Extended screen for a PC, see usb_display.h */
} usb_service_mode_t;

typedef enum {
    USB_DRIVE_STATUS_OFF,           /*!< Not in drive mode */
    USB_DRIVE_STATUS_WAITING_HOST,  /*!< Attached, but no host has configured the device yet */
    USB_DRIVE_STATUS_CONNECTED,     /*!< A host is using the drive */
    USB_DRIVE_STATUS_EJECTED,       /*!< The host ejected the drive; it is safe to stop */
} usb_drive_status_t;

/**
 * @brief  Export the microSD card to the USB host
 *
 *         Asks storage_service to release the card first, so applications close
 *         their files before the host sees the drive.
 */
esp_err_t usb_service_start_sd_drive(void);

/**
 * @brief  Attach as the extended screen; used by usb_display_start()
 */
esp_err_t usb_service_start_display(void);

/**
 * @brief  Detach from the host, and give the microSD card back to applications
 *         after drive mode
 */
esp_err_t usb_service_stop(void);

usb_service_mode_t usb_service_get_mode(void);
usb_drive_status_t usb_service_get_drive_status(void);

#ifdef __cplusplus
}
#endif
