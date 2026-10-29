/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/* Audio class settings come from usb_device_uac, which serves the speaker and microphone of
 * the extended screen; usb_descriptors.c lays its interfaces out next to the others. */
#include "uac_descriptors.h"
#include "tusb_config_uac.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_OS                 OPT_OS_FREERTOS
#define CFG_TUSB_OS_INC_PATH        freertos/
#define CFG_TUSB_DEBUG              0

#define CFG_TUSB_RHPORT1_MODE       (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_HIGH_SPEED

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE      64

/* Each mode only lists its own interfaces, so the other class drivers stay idle */

/* microSD drive */
#define CFG_TUD_MSC                 1
#define CFG_TUD_MSC_EP_BUFSIZE      (32 * 1024)

/* Extended screen: JPEG frames from the PC driver on a vendor interface, touch on HID. Without
 * FIFOs each callback is one packet, so a frame starts at a packet and its padding is dropped. */
#define CFG_TUD_VENDOR              1
#define CFG_TUD_VENDOR_EPSIZE       512
#define CFG_TUD_VENDOR_RX_BUFSIZE   0
#define CFG_TUD_VENDOR_TX_BUFSIZE   0
#define CFG_TUD_HID                 1
#define CFG_TUD_HID_EP_BUFSIZE      64

#ifdef __cplusplus
}
#endif
