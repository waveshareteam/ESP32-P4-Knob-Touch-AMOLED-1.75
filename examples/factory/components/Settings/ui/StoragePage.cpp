/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdio>
#include "esp_log.h"
#include "storage_service.h"
#include "usb_service.h"
#include "round_ui.hpp"
#include "StoragePage.hpp"

namespace esp_brookesia::apps {

static constexpr uint32_t REFRESH_MS = 500;
static const char *TAG = "StoragePage";

static void format_size(char *buf, size_t len, uint64_t bytes)
{
    if (bytes >= 1000ULL * 1000 * 1000) {
        snprintf(buf, len, "%.1f GB", bytes / 1e9);
    } else {
        snprintf(buf, len, "%.0f MB", bytes / 1e6);
    }
}

const void *StoragePage::icon() const
{
    return LV_SYMBOL_SD_CARD;
}

std::string StoragePage::summary() const
{
    switch (storage_sd_get_state()) {
    case STORAGE_SD_MOUNTED: {
        uint64_t total = 0;
        uint64_t free = 0;
        char free_str[16];
        if (storage_sd_get_usage(&total, &free) != ESP_OK) {
            return "Ready";
        }
        format_size(free_str, sizeof(free_str), free);
        return std::string(free_str) + " free";
    }
    case STORAGE_SD_USB:
        return "Open on a computer";
    case STORAGE_SD_NO_CARD:
        return "No card";
    case STORAGE_SD_UNFORMATTED:
        return "Not formatted";
    default:
        return "Card unreadable";
    }
}

void StoragePage::open(lv_obj_t *root)
{
    _column = round_ui::create_column(root);
    round_ui::create_section(_column, "microSD");
    _card = round_ui::create_note(_column);
    _rescan = round_ui::create_button(_column, LV_SYMBOL_REFRESH, "Rescan", false, rescan_cb, this);
    round_ui::create_section(_column, "Computer");
    _usb_switch = round_ui::create_switch_row(_column, LV_SYMBOL_USB, "USB drive", usb_switch_cb, this).control;
    _usb = round_ui::create_note(_column);
    refresh();
    _timer = lv_timer_create(timer_cb, REFRESH_MS, this);
}

void StoragePage::close()
{
    // The USB port only serves the drive while this page is on screen
    if (usb_service_get_mode() == USB_SERVICE_MODE_SD_DRIVE && usb_service_stop() != ESP_OK) {
        ESP_LOGW(TAG, "microSD was not remounted after USB");
    }
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    round_ui::dialog_close(_dialog);
    _dialog = nullptr;
    _column = _card = _rescan = _usb_switch = _usb = nullptr;
}

bool StoragePage::back()
{
    if (_dialog) {
        round_ui::dialog_close(_dialog);
        _dialog = nullptr;
        return true;
    }
    // Leaving stops the drive, so warn while the computer may still be writing
    if (usb_service_get_drive_status() == USB_DRIVE_STATUS_CONNECTED) {
        _dialog = round_ui::dialog_open("Drive still in use",
                                        "Leaving this page stops the drive. Eject it on the computer first, or stop "
                                        "it now and lose files being copied.",
                                        "Stop", true, dialog_cb, this);
        return true;
    }
    return false;
}

void StoragePage::pause()
{
    // Another app may want the port, and nothing here is on screen to turn the drive off
    if (usb_service_get_mode() == USB_SERVICE_MODE_SD_DRIVE) {
        round_ui::dialog_close(_dialog);
        _dialog = nullptr;
        stopUsbDrive();
    }
}

void StoragePage::knob(int steps)
{
    if (!_dialog) {
        round_ui::scroll_column(_column, steps);
    }
}

void StoragePage::refresh()
{
    char text[128];
    const storage_sd_state_t state = storage_sd_get_state();
    switch (state) {
    case STORAGE_SD_MOUNTED: {
        uint64_t total = 0;
        uint64_t free = 0;
        char total_str[16];
        char free_str[16];
        if (storage_sd_get_usage(&total, &free) == ESP_OK) {
            format_size(total_str, sizeof(total_str), total);
            format_size(free_str, sizeof(free_str), free);
            snprintf(text, sizeof(text), "Ready. %s free of %s.", free_str, total_str);
        } else {
            snprintf(text, sizeof(text), "Ready.");
        }
        break;
    }
    case STORAGE_SD_USB:
        snprintf(text, sizeof(text), "In use by the computer. Apps cannot open files until USB drive is off.");
        break;
    case STORAGE_SD_UNFORMATTED:
        snprintf(text, sizeof(text), "Not formatted. Turn on USB drive and format it as FAT32 on the computer.");
        break;
    case STORAGE_SD_NO_CARD:
        snprintf(text, sizeof(text), "No card. Insert a microSD card and tap Rescan.");
        break;
    case STORAGE_SD_ERROR:
    default:
        snprintf(text, sizeof(text), "The card could not be read. Tap Rescan to try again.");
        break;
    }
    lv_label_set_text(_card, text);
    round_ui::set_enabled(_rescan, state != STORAGE_SD_USB);
    round_ui::set_switch(_usb_switch, usb_service_get_mode() == USB_SERVICE_MODE_SD_DRIVE);

    switch (usb_service_get_drive_status()) {
    case USB_DRIVE_STATUS_WAITING_HOST:
        snprintf(text, sizeof(text), "Waiting for the computer. If no drive appears, flip the USB-C plug.");
        break;
    case USB_DRIVE_STATUS_CONNECTED:
        snprintf(text, sizeof(text), "Connected. Eject the drive on the computer before turning this off.");
        break;
    case USB_DRIVE_STATUS_EJECTED:
        snprintf(text, sizeof(text), "Ejected by the computer. It is safe to turn this off.");
        break;
    case USB_DRIVE_STATUS_OFF:
    default:
        snprintf(text, sizeof(text), "Turn on to open the card on a computer through the USB-C port, while this "
                 "page is open.");
        break;
    }
    lv_label_set_text(_usb, text);
}

void StoragePage::stopUsbDrive()
{
    if (usb_service_stop() != ESP_OK) {
        ESP_LOGW(TAG, "microSD was not remounted after USB");
    }
    refresh();
}

void StoragePage::timer_cb(lv_timer_t *timer)
{
    static_cast<StoragePage *>(lv_timer_get_user_data(timer))->refresh();
}

void StoragePage::rescan_cb(lv_event_t *e)
{
    StoragePage *page = static_cast<StoragePage *>(lv_event_get_user_data(e));
    storage_sd_remount();
    page->refresh();
}

void StoragePage::usb_switch_cb(lv_event_t *e)
{
    StoragePage *page = static_cast<StoragePage *>(lv_event_get_user_data(e));
    if (lv_obj_has_state(page->_usb_switch, LV_STATE_CHECKED)) {
        if (usb_service_start_sd_drive() != ESP_OK) {
            ESP_LOGW(TAG, "Start USB drive failed");
        }
        page->refresh();
        return;
    }
    if (usb_service_get_drive_status() != USB_DRIVE_STATUS_CONNECTED) {
        page->stopUsbDrive();
        return;
    }
    // The computer may still be writing; ask before pulling the drive away
    round_ui::set_switch(page->_usb_switch, true);
    if (!page->_dialog) {
        page->_dialog = round_ui::dialog_open("Drive still in use",
                                              "Eject it on the computer first. Stopping now can lose files being copied.",
                                              "Stop", true, dialog_cb, page);
    }
}

void StoragePage::dialog_cb(lv_event_t *e)
{
    StoragePage *page = static_cast<StoragePage *>(lv_event_get_user_data(e));
    const bool stop = round_ui::dialog_confirmed(e);
    round_ui::dialog_close(page->_dialog);
    page->_dialog = nullptr;
    if (stop) {
        page->stopUsbDrive();
    }
}

} // namespace esp_brookesia::apps
