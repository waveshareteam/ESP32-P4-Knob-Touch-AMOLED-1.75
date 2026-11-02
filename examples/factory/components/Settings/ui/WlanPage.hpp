/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>
#include <vector>
#include "wifi_service.h"
#include "SettingsPage.hpp"

namespace esp_brookesia::apps {

class PasswordView;

/**
 * @brief Wi-Fi on or off, the network in use, and joining another one from a scan
 *
 * The connection itself lives in the Wi-Fi service and keeps going when the page closes.
 */
class WlanPage: public SettingsPage {
public:
    ~WlanPage();

    const char *title() const override
    {
        return "WLAN";
    }
    const void *icon() const override;
    std::string summary() const override;
    void open(lv_obj_t *root) override;
    void close() override;
    bool back() override;
    void knob(int steps) override;

private:
    lv_obj_t *_root = nullptr;
    lv_obj_t *_column = nullptr;
    lv_obj_t *_switch = nullptr;
    lv_obj_t *_status = nullptr;
    lv_obj_t *_forget = nullptr;
    lv_obj_t *_networks_title = nullptr;
    lv_obj_t *_scan_note = nullptr;
    std::vector<lv_obj_t *> _rows;
    std::vector<lv_obj_t *> _row_values;
    std::vector<wifi_service_ap_t> _aps;
    uint32_t _shown_generation = UINT32_MAX;
    uint32_t _next_scan = 0;
    lv_timer_t *_timer = nullptr;
    lv_obj_t *_dialog = nullptr;
    PasswordView *_password = nullptr;
    std::string _password_ssid;

    void refresh();
    void showNetworks(const wifi_service_info_t &info);
    void addNetworkRow(int index);
    void openPassword(const std::string &ssid);
    void closePassword();

    static void timer_cb(lv_timer_t *timer);
    static void switch_cb(lv_event_t *e);
    static void forget_cb(lv_event_t *e);
    static void dialog_cb(lv_event_t *e);
    static void network_cb(lv_event_t *e);
    static void password_done_cb(void *ctx, const char *password);
    static void close_password_async(void *ctx);
};

} // namespace esp_brookesia::apps
