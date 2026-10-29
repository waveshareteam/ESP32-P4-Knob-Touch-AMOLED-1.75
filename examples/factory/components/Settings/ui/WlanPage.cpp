/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "app_fonts.h"
#include "round_ui.hpp"
#include "PasswordView.hpp"
#include "WlanPage.hpp"

LV_IMG_DECLARE(WIFI);
LV_IMG_DECLARE(wifi_1);
LV_IMG_DECLARE(wifi_2);
LV_IMG_DECLARE(wifi_3);
LV_IMG_DECLARE(wifi_4);

namespace esp_brookesia::apps {

static constexpr uint32_t REFRESH_MS = 500;
static constexpr uint32_t SCAN_PERIOD_MS = 15000;
static constexpr uint32_t SCAN_RETRY_MS = 2000;     // A scan can't start while a connection is set up
static const char *TAG = "WlanPage";

static bool tick_reached(uint32_t tick)
{
    return static_cast<int32_t>(lv_tick_get() - tick) >= 0;
}

static const lv_image_dsc_t *signal_image(int rssi)
{
    return rssi > -50 ? &wifi_4 : rssi > -65 ? &wifi_3 : rssi > -75 ? &wifi_2 : &wifi_1;
}

WlanPage::~WlanPage()
{
    delete _password;
}

const void *WlanPage::icon() const
{
    return &WIFI;
}

std::string WlanPage::summary() const
{
    wifi_service_info_t info;
    wifi_service_get_info(&info);
    switch (info.state) {
    case WIFI_SERVICE_OFF:
        return "Off";
    case WIFI_SERVICE_CONNECTED:
        return info.ssid;
    case WIFI_SERVICE_CONNECTING:
        return "Connecting...";
    case WIFI_SERVICE_IDLE:
    default:
        return info.fail == WIFI_SERVICE_FAIL_PASSWORD ? "Wrong password" : "Not connected";
    }
}

void WlanPage::open(lv_obj_t *root)
{
    _root = root;
    _column = round_ui::create_column(root);
    _switch = round_ui::create_switch_row(_column, &WIFI, "Wi-Fi", switch_cb, this).control;
    _status = round_ui::create_note(_column);
    _forget = round_ui::create_button(_column, LV_SYMBOL_TRASH, "Forget network", true, forget_cb, this);
    _networks_title = round_ui::create_section(_column, "Networks");
    _scan_note = round_ui::create_note(_column);
    _shown_generation = UINT32_MAX;
    _next_scan = lv_tick_get();
    refresh();
    _timer = lv_timer_create(timer_cb, REFRESH_MS, this);
}

void WlanPage::close()
{
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    // Not from inside one of its own events here, so it can go at once
    delete _password;
    _password = nullptr;
    round_ui::dialog_close(_dialog);
    _dialog = nullptr;
    _rows.clear();
    _row_values.clear();
    _aps.clear();
    _root = _column = _switch = _status = _forget = _networks_title = _scan_note = nullptr;
}

bool WlanPage::back()
{
    if (_dialog) {
        round_ui::dialog_close(_dialog);
        _dialog = nullptr;
        return true;
    }
    if (_password) {
        closePassword();
        return true;
    }
    return false;
}

void WlanPage::knob(int steps)
{
    if (_password) {
        _password->knob(steps);
    } else if (!_dialog) {
        round_ui::scroll_column(_column, steps);
    }
}

void WlanPage::refresh()
{
    wifi_service_info_t info;
    wifi_service_get_info(&info);
    const bool on = info.state != WIFI_SERVICE_OFF;
    round_ui::set_switch(_switch, on);

    char text[160];
    switch (info.state) {
    case WIFI_SERVICE_OFF:
        snprintf(text, sizeof(text), "Off. Turn on to join a network.");
        break;
    case WIFI_SERVICE_CONNECTING:
        snprintf(text, sizeof(text), "Joining %s...", info.ssid);
        break;
    case WIFI_SERVICE_CONNECTED:
        snprintf(text, sizeof(text), "Connected to %s. IP address %s.", info.ssid, info.ip);
        break;
    case WIFI_SERVICE_IDLE:
    default:
        if (!info.ssid[0]) {
            snprintf(text, sizeof(text), "Not connected. Choose a network below.");
        } else if (info.fail == WIFI_SERVICE_FAIL_PASSWORD) {
            snprintf(text, sizeof(text), "Wrong password for %s. Choose it below to try again.", info.ssid);
        } else if (info.fail == WIFI_SERVICE_FAIL_NOT_FOUND) {
            snprintf(text, sizeof(text), "%s is out of range. Trying again now and then.", info.ssid);
        } else {
            snprintf(text, sizeof(text), "Could not join %s. Trying again now and then.", info.ssid);
        }
        break;
    }
    lv_label_set_text(_status, text);
    if (on && info.ssid[0]) {
        lv_obj_remove_flag(_forget, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_forget, LV_OBJ_FLAG_HIDDEN);
    }

    if (!on) {
        lv_obj_add_flag(_networks_title, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_scan_note, LV_OBJ_FLAG_HIDDEN);
        for (lv_obj_t *row : _rows) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_remove_flag(_networks_title, LV_OBJ_FLAG_HIDDEN);
    // Look for networks every now and then while the page is open
    if (!_password && !wifi_service_is_scanning() && tick_reached(_next_scan)) {
        _next_scan = lv_tick_get() + (wifi_service_scan() == ESP_OK ? SCAN_PERIOD_MS : SCAN_RETRY_MS);
    }
    showNetworks(info);
}

void WlanPage::showNetworks(const wifi_service_info_t &info)
{
    wifi_service_ap_t aps[WIFI_SERVICE_SCAN_MAX];
    uint32_t generation = 0;
    const int count = wifi_service_get_scan(aps, WIFI_SERVICE_SCAN_MAX, &generation);
    if (generation == 0 && wifi_service_is_scanning()) {
        lv_label_set_text(_scan_note, "Searching...");
    } else {
        lv_label_set_text(_scan_note, count == 0 ? "No networks found." : "");
    }
    if (lv_label_get_text(_scan_note)[0]) {
        lv_obj_remove_flag(_scan_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_scan_note, LV_OBJ_FLAG_HIDDEN);
    }
    if (generation != _shown_generation) {
        _shown_generation = generation;
        for (lv_obj_t *row : _rows) {
            lv_obj_delete(row);
        }
        _rows.clear();
        _row_values.clear();
        _aps.assign(aps, aps + count);
        for (int i = 0; i < count; i++) {
            addNetworkRow(i);
        }
    }
    // The network in use says so
    for (size_t i = 0; i < _rows.size(); i++) {
        const bool current = strcmp(_aps[i].ssid, info.ssid) == 0;
        const char *value = _aps[i].secure ? "Secured" : "Open";
        if (current && info.state == WIFI_SERVICE_CONNECTED) {
            value = "Connected";
        } else if (current && info.state == WIFI_SERVICE_CONNECTING) {
            value = "Joining...";
        }
        if (strcmp(lv_label_get_text(_row_values[i]), value) != 0) {
            lv_label_set_text(_row_values[i], value);
        }
        lv_obj_remove_flag(_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void WlanPage::addNetworkRow(int index)
{
    const wifi_service_ap_t &ap = _aps[index];
    round_ui::Row row = round_ui::create_row(_column, signal_image(ap.rssi), ap.ssid, ap.secure ? "Secured" : "Open",
                                             true);
    // Network names may be Chinese
    lv_obj_set_style_text_font(row.title, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_style_max_height(row.title, lv_font_get_line_height(app_font_text_20()), LV_PART_MAIN);
    lv_obj_set_user_data(row.obj, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    lv_obj_add_event_cb(row.obj, network_cb, LV_EVENT_CLICKED, this);
    _rows.push_back(row.obj);
    _row_values.push_back(row.value);
}

void WlanPage::openPassword(const std::string &ssid)
{
    _password_ssid = ssid;
    _password = new PasswordView(_root, ssid.c_str(), password_done_cb, this);
}

void WlanPage::closePassword()
{
    delete _password;
    _password = nullptr;
}

void WlanPage::timer_cb(lv_timer_t *timer)
{
    static_cast<WlanPage *>(lv_timer_get_user_data(timer))->refresh();
}

void WlanPage::switch_cb(lv_event_t *e)
{
    WlanPage *page = static_cast<WlanPage *>(lv_event_get_user_data(e));
    const bool on = lv_obj_has_state(page->_switch, LV_STATE_CHECKED);
    if (wifi_service_set_enabled(on) != ESP_OK) {
        ESP_LOGW(TAG, "Switch Wi-Fi %s failed", on ? "on" : "off");
    }
    page->_next_scan = lv_tick_get();
    page->refresh();
}

void WlanPage::forget_cb(lv_event_t *e)
{
    WlanPage *page = static_cast<WlanPage *>(lv_event_get_user_data(e));
    if (!page->_dialog) {
        page->_dialog = round_ui::dialog_open("Forget network?", "Joining it again will need its password.",
                                              "Forget", true, dialog_cb, page);
    }
}

void WlanPage::dialog_cb(lv_event_t *e)
{
    WlanPage *page = static_cast<WlanPage *>(lv_event_get_user_data(e));
    const bool forget = round_ui::dialog_confirmed(e);
    round_ui::dialog_close(page->_dialog);
    page->_dialog = nullptr;
    if (forget) {
        wifi_service_forget();
        page->refresh();
    }
}

void WlanPage::network_cb(lv_event_t *e)
{
    WlanPage *page = static_cast<WlanPage *>(lv_event_get_user_data(e));
    const size_t index = reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e)));
    if (index >= page->_aps.size() || page->_password) {
        return;
    }
    const wifi_service_ap_t &ap = page->_aps[index];
    if (ap.secure) {
        page->openPassword(ap.ssid);
    } else {
        wifi_service_connect(ap.ssid, nullptr);
        page->refresh();
    }
}

void WlanPage::password_done_cb(void *ctx, const char *password)
{
    WlanPage *page = static_cast<WlanPage *>(ctx);
    if (password) {
        if (wifi_service_connect(page->_password_ssid.c_str(), password) != ESP_OK) {
            ESP_LOGW(TAG, "Join %s failed", page->_password_ssid.c_str());
        }
    }
    // Called from a button of the view, which can't delete itself in its own event
    lv_async_call(close_password_async, page);
}

void WlanPage::close_password_async(void *ctx)
{
    WlanPage *page = static_cast<WlanPage *>(ctx);
    page->closePassword();
    if (page->_status) {
        page->refresh();
    }
}

} // namespace esp_brookesia::apps
