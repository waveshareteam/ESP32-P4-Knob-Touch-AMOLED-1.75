/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdio>
#include "bt_service.h"
#include "round_ui.hpp"
#include "BluetoothPage.hpp"

LV_IMG_DECLARE(BLE);

namespace esp_brookesia::apps {

static constexpr uint32_t REFRESH_MS = 500;

const void *BluetoothPage::icon() const
{
    return &BLE;
}

std::string BluetoothPage::summary() const
{
    bt_info_t info;
    bt_service_get_info(&info);
    switch (info.state) {
    case BT_STATE_OFF:
        return "Off";
    case BT_STATE_CONNECTED:
    case BT_STATE_STREAMING:
        return info.peer_name[0] ? info.peer_name : "Connected";
    case BT_STATE_WAITING:
    default:
        return "On";
    }
}

void BluetoothPage::open(lv_obj_t *root)
{
    _column = round_ui::create_column(root);
    _switch = round_ui::create_switch_row(_column, &BLE, "Bluetooth", switch_cb, this).control;
    _status = round_ui::create_note(_column);
    _disconnect = round_ui::create_button(_column, LV_SYMBOL_CLOSE, "Disconnect", false, disconnect_cb, this);
    _forget = round_ui::create_button(_column, LV_SYMBOL_TRASH, "Forget phones", true, forget_cb, this);
    round_ui::set_switch(_switch, bt_service_is_enabled());
    refresh();
    _timer = lv_timer_create(timer_cb, REFRESH_MS, this);
}

void BluetoothPage::close()
{
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    round_ui::dialog_close(_dialog);
    _dialog = nullptr;
    _column = _switch = _status = _disconnect = _forget = nullptr;
}

bool BluetoothPage::back()
{
    if (_dialog) {
        round_ui::dialog_close(_dialog);
        _dialog = nullptr;
        return true;
    }
    return false;
}

void BluetoothPage::knob(int steps)
{
    if (!_dialog) {
        round_ui::scroll_column(_column, steps);
    }
}

void BluetoothPage::refresh()
{
    bt_info_t info;
    bt_service_get_info(&info);
    const char *peer = info.peer_name[0] ? info.peer_name : "your phone";
    char text[192];
    switch (info.state) {
    case BT_STATE_OFF:
        snprintf(text, sizeof(text), "Off. Turn on to play music from a phone.");
        break;
    case BT_STATE_WAITING:
        snprintf(text, sizeof(text), "Visible as \"%s\". Pair it in your phone's Bluetooth settings.", info.local_name);
        break;
    case BT_STATE_CONNECTED:
        snprintf(text, sizeof(text), "Connected to %s. Play music on the phone or open BT Music.", peer);
        break;
    case BT_STATE_STREAMING:
        snprintf(text, sizeof(text), "Playing from %s.", peer);
        break;
    default:
        text[0] = '\0';
        break;
    }
    lv_label_set_text(_status, text);
    round_ui::set_enabled(_disconnect, info.state == BT_STATE_CONNECTED || info.state == BT_STATE_STREAMING);
    round_ui::set_enabled(_forget, info.state != BT_STATE_OFF);
}

void BluetoothPage::timer_cb(lv_timer_t *timer)
{
    static_cast<BluetoothPage *>(lv_timer_get_user_data(timer))->refresh();
}

void BluetoothPage::switch_cb(lv_event_t *e)
{
    BluetoothPage *page = static_cast<BluetoothPage *>(lv_event_get_user_data(e));
    bt_service_set_enabled(lv_obj_has_state(page->_switch, LV_STATE_CHECKED));
    page->refresh();
}

void BluetoothPage::disconnect_cb(lv_event_t *e)
{
    BluetoothPage *page = static_cast<BluetoothPage *>(lv_event_get_user_data(e));
    bt_service_disconnect();
    page->refresh();
}

void BluetoothPage::forget_cb(lv_event_t *e)
{
    BluetoothPage *page = static_cast<BluetoothPage *>(lv_event_get_user_data(e));
    if (!page->_dialog) {
        page->_dialog = round_ui::dialog_open("Forget phones?",
                                              "Paired phones will have to pair again before they can connect.",
                                              "Forget", true, dialog_cb, page);
    }
}

void BluetoothPage::dialog_cb(lv_event_t *e)
{
    BluetoothPage *page = static_cast<BluetoothPage *>(lv_event_get_user_data(e));
    const bool forget = round_ui::dialog_confirmed(e);
    round_ui::dialog_close(page->_dialog);
    page->_dialog = nullptr;
    if (forget) {
        bt_service_forget_all();
        page->refresh();
    }
}

} // namespace esp_brookesia::apps
