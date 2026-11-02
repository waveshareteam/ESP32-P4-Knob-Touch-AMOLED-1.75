/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "board_app.h"
#include "sdkconfig.h"
#include "usb_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The extended screen is the whole panel; the PC driver reads this size and the encoding from the
 * vendor interface string, and the HID touch range matches it */
#define USB_DISPLAY_WIDTH           BOARD_LCD_H_RES
#define USB_DISPLAY_HEIGHT          BOARD_LCD_V_RES
#define USB_DISPLAY_JPEG_QUALITY    8
#define USB_DISPLAY_MAX_FPS         60
#define USB_DISPLAY_FRAME_LIMIT     (300 * 1024)
/* Buffers also hold an uncompressed frame of the whole panel, in case the driver sends one */
#define USB_DISPLAY_BUFFER_SIZE     (USB_DISPLAY_WIDTH * USB_DISPLAY_HEIGHT * 3)
#define USB_DISPLAY_TOUCH_POINTS    USB_DISPLAY_TOUCH_POINTS_MAX

/* Interfaces of the extended screen; the audio function takes three from ITF_DISPLAY_AUDIO */
enum {
    ITF_DISPLAY_VENDOR = 0,
    ITF_DISPLAY_HID,
    ITF_DISPLAY_AUDIO_CONTROL,
    ITF_DISPLAY_AUDIO_SPK,
    ITF_DISPLAY_AUDIO_MIC,
    ITF_DISPLAY_TOTAL,
};

enum {
    REPORT_ID_TOUCH = 1,
    REPORT_ID_MAX_COUNT,
};

#ifdef __cplusplus
}
#endif
