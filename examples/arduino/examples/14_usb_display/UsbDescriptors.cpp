/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstring>

extern "C" {
#include "esp_mac.h"
#include "tusb.h"
}

namespace {

constexpr uint8_t kVendorOutEndpoint = 0x01;
constexpr uint8_t kVendorInEndpoint = 0x81;
constexpr uint8_t kHidInEndpoint = 0x82;
constexpr uint8_t kTouchReportId = 1;
constexpr uint8_t kMaxCountReportId = 2;
constexpr uint8_t kTouchPoints = 5;

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

const uint8_t kHidReport[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DIGITIZER), HID_USAGE(0x04), HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(kTouchReportId)
    TOUCH_FINGER_USAGE(466, 466), TOUCH_FINGER_USAGE(466, 466), TOUCH_FINGER_USAGE(466, 466),
    TOUCH_FINGER_USAGE(466, 466), TOUCH_FINGER_USAGE(466, 466),
    HID_USAGE(0x54), HID_LOGICAL_MAX(127), HID_REPORT_COUNT(1), HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_REPORT_ID(kMaxCountReportId) HID_USAGE(0x55), HID_REPORT_COUNT(1),
    HID_LOGICAL_MAX(kTouchPoints), HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};

constexpr uint16_t kConfigLength = TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + TUD_HID_DESC_LEN;
const uint8_t kConfiguration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, kConfigLength, 0, 500),
    TUD_VENDOR_DESCRIPTOR(0, 4, kVendorOutEndpoint, kVendorInEndpoint, 512),
    TUD_HID_DESCRIPTOR(1, 5, HID_ITF_PROTOCOL_NONE, sizeof(kHidReport), kHidInEndpoint, 64, 4),
};

const tusb_desc_device_t kDevice = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bDeviceClass = 0, .bDeviceSubClass = 0, .bDeviceProtocol = 0, .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A, .idProduct = 0x2986, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};

}  // namespace

extern "C" uint8_t const *tud_descriptor_device_cb(void)
{
    return reinterpret_cast<const uint8_t *>(&kDevice);
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t)
{
    return kConfiguration;
}

extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t)
{
    return kHidReport;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t)
{
    static uint16_t descriptor[64];
    static char serial[13] = {};
    if (index == 0) {
        descriptor[1] = 0x0409;
        descriptor[0] = (TUSB_DESC_STRING << 8) | 4;
        return descriptor;
    }
    if (index == 3 && serial[0] == '\0') {
        uint8_t mac[6] = {};
        esp_efuse_mac_get_default(mac);
        snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    const char *value = nullptr;
    switch (index) {
    case 1: value = "Waveshare"; break;
    case 2: value = "ESP32-P4 Knob Screen"; break;
    case 3: value = serial; break;
    case 4: value = "esp32p4udisp0_R466x466_Ejpg8_Fps60_Bl307200"; break;
    case 5: value = "Touch"; break;
    default: return nullptr;
    }
    size_t length = strlen(value);
    if (length > 63) {
        length = 63;
    }
    for (size_t i = 0; i < length; ++i) {
        descriptor[i + 1] = value[i];
    }
    descriptor[0] = (TUSB_DESC_STRING << 8) | (2 * length + 2);
    return descriptor;
}
