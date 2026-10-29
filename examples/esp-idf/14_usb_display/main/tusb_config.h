/* SPDX-FileCopyrightText: 2026 Waveshare */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "sdkconfig.h"

#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUSB_OS_INC_PATH freertos/
#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_RHPORT1_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#define CFG_TUD_ENABLED 1
#define CFG_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_VENDOR 1
#define CFG_TUD_VENDOR_EPSIZE 512
#define CFG_TUD_VENDOR_RX_BUFSIZE 0
#define CFG_TUD_VENDOR_TX_BUFSIZE 0
#define CFG_TUD_HID 1
#define CFG_TUD_HID_EP_BUFSIZE 64
