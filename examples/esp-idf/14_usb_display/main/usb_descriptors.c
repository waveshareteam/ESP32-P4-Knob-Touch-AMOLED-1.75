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

#define USB_VID 0x303A
#define USB_PID_DISPLAY 0x2986
#define EPNUM_VENDOR_OUT 0x01
#define EPNUM_VENDOR_IN 0x81
#define EPNUM_HID_IN 0x82
#define HID_POLL_INTERVAL 4

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_VENDOR,
    STRID_HID,
};

#define _STRINGIFY(value) #value
#define STRINGIFY(value) _STRINGIFY(value)
#define DISPLAY_VENDOR_STR CONFIG_IDF_TARGET "udisp0_R" STRINGIFY(USB_DISPLAY_WIDTH) "x" STRINGIFY(USB_DISPLAY_HEIGHT) \
    "_Ejpg8_Fps60_Bl307200"

#define TOUCH_FINGER_USAGE(width, height) \
    HID_USAGE(0x42), HID_COLLECTION(HID_COLLECTION_LOGICAL), \
    HID_USAGE(0x42), HID_LOGICAL_MIN(0x00), HID_LOGICAL_MAX(0x01), HID_REPORT_SIZE(1), HID_REPORT_COUNT(1), \
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), HID_REPORT_COUNT(7), HID_INPUT(HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE), \
    HID_REPORT_SIZE(8), HID_USAGE(0x51), HID_REPORT_COUNT(1), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP), HID_LOGICAL_MAX_N(width, 2), HID_REPORT_SIZE(16), HID_UNIT_EXPONENT(0x0e), \
    HID_UNIT(0x13), HID_USAGE(0x30), HID_PHYSICAL_MIN(0), HID_PHYSICAL_MAX_N(width, 2), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_LOGICAL_MAX_N(height, 2), HID_PHYSICAL_MAX_N(height, 2), HID_USAGE(0x31), HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_USAGE_PAGE(HID_USAGE_PAGE_DIGITIZER), HID_USAGE(0x48), HID_USAGE(0x49), HID_REPORT_COUNT(2), \
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), HID_COLLECTION_END

static const uint8_t s_hid_report[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DIGITIZER), HID_USAGE(0x04), HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(USB_DISPLAY_REPORT_ID_TOUCH)
    TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
    TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
    TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
    TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
    TOUCH_FINGER_USAGE(USB_DISPLAY_WIDTH, USB_DISPLAY_HEIGHT),
    HID_USAGE(0x54), HID_LOGICAL_MAX(127), HID_REPORT_COUNT(1), HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_REPORT_ID(USB_DISPLAY_REPORT_ID_MAX_COUNT) HID_USAGE(0x55), HID_REPORT_COUNT(1),
    HID_LOGICAL_MAX(USB_DISPLAY_TOUCH_POINTS), HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

#define DISPLAY_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t s_display_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, DISPLAY_CONFIG_TOTAL_LEN, 0, 500),
    TUD_VENDOR_DESCRIPTOR(0, STRID_VENDOR, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, CFG_TUD_VENDOR_EPSIZE),
    TUD_HID_DESCRIPTOR(1, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report), EPNUM_HID_IN,
                       CFG_TUD_HID_EP_BUFSIZE, HID_POLL_INTERVAL),
};

static const tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID, .idProduct = USB_PID_DISPLAY, .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER, .iProduct = STRID_PRODUCT, .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&s_device_descriptor; }
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return s_display_descriptor; }
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { (void)instance; return s_hid_report; }

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t descriptor[64];
    static char serial[13];
    const char *value = NULL;
    if (index == STRID_LANGID) {
        descriptor[1] = 0x0409;
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        return descriptor;
    }
    if (index == STRID_SERIAL && !serial[0]) {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    switch (index) {
    case STRID_MANUFACTURER: value = "Waveshare"; break;
    case STRID_PRODUCT: value = "ESP32-P4 Knob Screen"; break;
    case STRID_SERIAL: value = serial; break;
    case STRID_VENDOR: value = DISPLAY_VENDOR_STR; break;
    case STRID_HID: value = "Touch"; break;
    default: return NULL;
    }
    size_t length = strlen(value);
    if (length > 63) length = 63;
    for (size_t i = 0; i < length; ++i) descriptor[i + 1] = value[i];
    descriptor[0] = (TUSB_DESC_STRING << 8) | (2 * length + 2);
    return descriptor;
}
