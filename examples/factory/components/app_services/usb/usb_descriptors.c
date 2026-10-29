/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_mac.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "usb_service.h"

/* Espressif VID. The microSD drive uses the PID TinyUSB assigns to an MSC-only device; the
 * extended screen uses the one the Espressif USB display driver for Windows binds to. */
#define USB_VID             0x303A
#define USB_PID_SD_DRIVE    0x4002
#define USB_PID_DISPLAY     0x2986

#define EPNUM_MSC_OUT       0x01
#define EPNUM_MSC_IN        0x81
#define MSC_EP_SIZE_HS      512
#define MSC_EP_SIZE_FS      64

#define EPNUM_VENDOR_OUT    0x01
#define EPNUM_VENDOR_IN     0x81
#define EPNUM_HID_IN        0x82
#define EPNUM_AUDIO_OUT     0x03
#define EPNUM_AUDIO_IN      0x84
#define EPNUM_AUDIO_FB      0x85
/* 2^(4-1) high-speed microframes: one touch report per millisecond at most */
#define HID_POLL_INTERVAL   4

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MSC,
    STRID_VENDOR,
    STRID_HID,
    STRID_AUDIO,        // The audio function names its streaming interfaces with the next two
    STRID_AUDIO_SPK,
    STRID_AUDIO_MIC,
};

#define _STRINGIFY(x)       #x
#define STRINGIFY(x)        _STRINGIFY(x)

/* Read by the PC driver: panel size, JPEG at this quality, frame rate cap and largest frame */
#define DISPLAY_VENDOR_STR  CONFIG_IDF_TARGET "udisp0_R" STRINGIFY(USB_DISPLAY_WIDTH) "x" STRINGIFY(USB_DISPLAY_HEIGHT) \
                            "_Ejpg" STRINGIFY(USB_DISPLAY_JPEG_QUALITY) "_Fps" STRINGIFY(USB_DISPLAY_MAX_FPS) \
                            "_Bl" STRINGIFY(USB_DISPLAY_FRAME_LIMIT)

/* ---------------------------------------------------------------------------
 * Device
 * ------------------------------------------------------------------------- */

static tusb_desc_device_t s_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID_SD_DRIVE,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

static const tusb_desc_device_qualifier_t s_desc_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0x00,
};

/* ---------------------------------------------------------------------------
 * microSD drive
 * ------------------------------------------------------------------------- */

#define MSC_CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t s_desc_msc_hs[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, MSC_CONFIG_TOTAL_LEN, 0, 500),
    TUD_MSC_DESCRIPTOR(0, STRID_MSC, EPNUM_MSC_OUT, EPNUM_MSC_IN, MSC_EP_SIZE_HS),
};

static const uint8_t s_desc_msc_fs[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, MSC_CONFIG_TOTAL_LEN, 0, 500),
    TUD_MSC_DESCRIPTOR(0, STRID_MSC, EPNUM_MSC_OUT, EPNUM_MSC_IN, MSC_EP_SIZE_FS),
};

/* Other-speed descriptor: same layout, but the descriptor type is OTHER_SPEED_CONFIG */
static uint8_t s_desc_other_speed[MSC_CONFIG_TOTAL_LEN];

/* ---------------------------------------------------------------------------
 * Extended screen
 * ------------------------------------------------------------------------- */

/* One finger of a multi-touch screen: tip switch, contact id, X, Y, width and height */
#define TOUCH_FINGER_USAGE(width, height) \
    HID_USAGE(0x42), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_USAGE(0x42), \
        HID_LOGICAL_MIN(0x00), \
        HID_LOGICAL_MAX(0x01), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(1), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_REPORT_COUNT(7), \
        HID_INPUT(HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE), \
        HID_REPORT_SIZE(8), \
        HID_USAGE(0x51), \
        HID_REPORT_COUNT(1), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP), \
        HID_LOGICAL_MAX_N(width, 2), \
        HID_REPORT_SIZE(16), \
        HID_UNIT_EXPONENT(0x0e), \
        HID_UNIT(0x13), \
        HID_USAGE(0x30), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(width, 2), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_LOGICAL_MAX_N(height, 2), \
        HID_PHYSICAL_MAX_N(height, 2), \
        HID_USAGE(0x31), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_DIGITIZER), \
        HID_USAGE(0x48), \
        HID_USAGE(0x49), \
        HID_REPORT_COUNT(2), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_COLLECTION_END

/* Touch screen with USB_DISPLAY_TOUCH_POINTS fingers, a contact count and the maximum count */
static const uint8_t s_desc_hid_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DIGITIZER),
    HID_USAGE(0x04),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(REPORT_ID_TOUCH)
        TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
        TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
        TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
        TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
        TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
        HID_USAGE(0x54),
        HID_LOGICAL_MAX(127),
        HID_REPORT_COUNT(1),
        HID_REPORT_SIZE(8),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        HID_REPORT_ID(REPORT_ID_MAX_COUNT)
        HID_USAGE(0x55),
        HID_REPORT_COUNT(1),
        HID_LOGICAL_MAX(0x10),
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

#define DISPLAY_CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_HID_DESC_LEN + \
                                     TUD_AUDIO_DEVICE_DESC_LEN)

static const uint8_t s_desc_display[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_DISPLAY_TOTAL, 0, DISPLAY_CONFIG_TOTAL_LEN, 0, 500),
    TUD_VENDOR_DESCRIPTOR(ITF_DISPLAY_VENDOR, STRID_VENDOR, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, CFG_TUD_VENDOR_EPSIZE),
    TUD_HID_DESCRIPTOR(ITF_DISPLAY_HID, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_desc_hid_report), EPNUM_HID_IN,
                       CFG_TUD_HID_EP_BUFSIZE, HID_POLL_INTERVAL),
    TUD_AUDIO_DESCRIPTOR(ITF_DISPLAY_AUDIO_CONTROL, STRID_AUDIO, EPNUM_AUDIO_OUT, EPNUM_AUDIO_IN, EPNUM_AUDIO_FB),
};

/* ---------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static bool display_mode(void)
{
    return usb_service_get_mode() == USB_SERVICE_MODE_DISPLAY;
}

uint8_t const *tud_descriptor_device_cb(void)
{
    s_desc_device.idProduct = display_mode() ? USB_PID_DISPLAY : USB_PID_SD_DRIVE;
    return (uint8_t const *)&s_desc_device;
}

uint8_t const *tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const *)&s_desc_qualifier;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    if (display_mode()) {
        return s_desc_display;
    }
    return tud_speed_get() == TUSB_SPEED_HIGH ? s_desc_msc_hs : s_desc_msc_fs;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    if (display_mode()) {
        // The screen needs high speed; a full-speed host gets nothing to switch to
        return NULL;
    }
    const uint8_t *other = tud_speed_get() == TUSB_SPEED_HIGH ? s_desc_msc_fs : s_desc_msc_hs;
    memcpy(s_desc_other_speed, other, MSC_CONFIG_TOTAL_LEN);
    s_desc_other_speed[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return s_desc_other_speed;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_desc_hid_report;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t desc_str[64];
    static char serial[13];
    const char *str;
    size_t chr_count;

    switch (index) {
    case STRID_LANGID:
        desc_str[1] = 0x0409;
        chr_count = 1;
        desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
        return desc_str;
    case STRID_MANUFACTURER:
        str = "Waveshare";
        break;
    case STRID_PRODUCT:
        str = display_mode() ? "ESP32-P4 Knob Screen" : "ESP32-P4 Knob microSD";
        break;
    case STRID_SERIAL:
        if (serial[0] == '\0') {
            uint8_t mac[6] = {0};
            esp_efuse_mac_get_default(mac);
            snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        }
        str = serial;
        break;
    case STRID_MSC:
        str = "microSD";
        break;
    case STRID_VENDOR:
        str = DISPLAY_VENDOR_STR;
        break;
    case STRID_HID:
        str = "Touch";
        break;
    case STRID_AUDIO:
        str = "Knob Screen Audio";
        break;
    case STRID_AUDIO_SPK:
        str = "Speaker";
        break;
    case STRID_AUDIO_MIC:
        str = "Microphone";
        break;
    default:
        return NULL;
    }

    chr_count = strlen(str);
    if (chr_count > 63) {
        chr_count = 63;
    }
    for (size_t i = 0; i < chr_count; i++) {
        desc_str[1 + i] = str[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}
