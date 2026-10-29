/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fingers in one touch report */
#define USB_DISPLAY_TOUCH_POINTS_MAX    5

/**
 * The board as an extra screen for a Windows 10/11 PC running the Espressif USB display
 * driver: the PC sends JPEG frames of the whole panel, gets touches back as a touch screen,
 * and plays and records sound through the board's speaker and microphones.
 *
 * It takes the USB port like the microSD drive does, so only one of them runs at a time.
 */
typedef enum {
    USB_DISPLAY_OFF,            /*!< Not started */
    USB_DISPLAY_WAITING_HOST,   /*!< Attached, but no PC is using the device, or it sleeps */
    USB_DISPLAY_CONNECTED,      /*!< A PC is connected but sends no picture */
    USB_DISPLAY_STREAMING,      /*!< Frames are arriving */
} usb_display_status_t;

/* Pixel data of a frame, numbered as in the PC driver's protocol */
typedef enum {
    USB_DISPLAY_FORMAT_RGB565 = 0,
    USB_DISPLAY_FORMAT_RGB888 = 1,
    USB_DISPLAY_FORMAT_JPEG = 3,
} usb_display_format_t;

/* A picture for a rectangle of the panel: the whole panel, or the part that changed */
typedef struct {
    uint8_t *data;              /*!< In a buffer the JPEG decoder and the PPA can read; the holder may
                                     change it, for example to patch the JPEG header */
    size_t len;
    usb_display_format_t format;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} usb_display_frame_t;

/* What arrived from the PC since usb_display_start(), for troubleshooting */
typedef struct {
    uint32_t packets;           /*!< Vendor packets */
    uint32_t frames;            /*!< Complete frames */
    uint32_t busy_drops;        /*!< Frames dropped while every buffer waited to be shown */
    uint32_t bad_format;        /*!< Headers with a pixel format this screen cannot show */
    uint32_t bad_rect;          /*!< Headers for a rectangle outside the panel */
    uint32_t bad_len;           /*!< Headers announcing more data than a buffer holds, or the wrong amount */
    uint8_t last_type;          /*!< Last header seen */
    uint16_t last_x;
    uint16_t last_y;
    uint16_t last_width;
    uint16_t last_height;
    uint32_t last_len;
} usb_display_stats_t;

typedef struct {
    uint8_t id;                 /*!< Stays the same while the finger is down */
    uint16_t x;
    uint16_t y;
} usb_display_touch_t;

/**
 * @brief  Take the USB port and appear as the screen
 *
 * @return ESP_ERR_INVALID_STATE when the port is in use, for example by the microSD drive
 */
esp_err_t usb_display_start(void);

/**
 * @brief  Leave the PC and give the port and the audio back
 */
esp_err_t usb_display_stop(void);

usb_display_status_t usb_display_get_status(void);

void usb_display_get_stats(usb_display_stats_t *stats);

/**
 * @brief  Wait for the next complete frame; hand it back with usb_display_return_frame()
 *
 * @return NULL after timeout_ms
 */
usb_display_frame_t *usb_display_get_frame(uint32_t timeout_ms);

void usb_display_return_frame(usb_display_frame_t *frame);

/**
 * @brief  Report the fingers now on the screen, in panel pixels; none means all lifted
 *
 * @return ESP_ERR_NOT_FINISHED when the PC has not taken the last report yet; send again later
 */
esp_err_t usb_display_report_touch(const usb_display_touch_t *points, size_t count);

#ifdef __cplusplus
}
#endif
