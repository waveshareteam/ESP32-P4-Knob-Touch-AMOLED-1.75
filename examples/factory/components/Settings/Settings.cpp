/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include "Settings.hpp"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Settings"
#include "esp_lib_utils.h"
#include "audio_service.h"
#include "board_app.h"
#include "knob_service.h"
#include "wifi_service.h"
#include "ui/round_ui.hpp"
#include "ui/AboutPage.hpp"
#include "ui/BluetoothPage.hpp"
#include "ui/LevelPage.hpp"
#include "ui/StoragePage.hpp"
#include "ui/VibrationPage.hpp"
#include "ui/WlanPage.hpp"

LV_IMG_DECLARE(sound);
LV_IMG_DECLARE(backlights);
LV_IMG_DECLARE(img_app_settings);

namespace esp_brookesia::apps
{
    static constexpr uint32_t REFRESH_MS = 1000;
    static constexpr int MIN_BRIGHTNESS = 20;   // Darker than this the panel is hard to find again

    static constexpr int WLAN_PAGE = 0;
    static constexpr int RETURN_DELAY_TICKS = 2;   // Refreshes to show "Connected" before going back

    Settings *Settings::_instance = nullptr;
    static bool s_wlan_requested = false;
    static int s_wlan_return_app = -1;
    static int s_wlan_connected_ticks = 0;

    Settings *Settings::requestInstance(bool use_status_bar, bool use_navigation_bar)
    {
        if (_instance == nullptr)
        {
            _instance = new Settings(use_status_bar, use_navigation_bar);
        }
        return _instance;
    }

    Settings::Settings(bool use_status_bar, bool use_navigation_bar) :
        App("Settings", &img_app_settings, true, use_status_bar, use_navigation_bar)
    {
        _pages.emplace_back(new WlanPage());
        _pages.emplace_back(new BluetoothPage());
        _pages.emplace_back(new LevelPage({
            .title = "Sound",
            .list_icon = &sound,
            .symbol = LV_SYMBOL_VOLUME_MAX,
            .caption = "Volume",
            .min = 0,
            .get = audio_service_get_volume,
            .set = [](int percent) { audio_service_set_volume(percent); },
            // The music apps, the knob in them and a connected phone change it too
            .follow = true,
        }));
        _pages.emplace_back(new LevelPage({
            .title = "Display",
            .list_icon = &backlights,
            .symbol = LV_SYMBOL_IMAGE,
            .caption = "Brightness",
            .min = MIN_BRIGHTNESS,
            .get = board_display_brightness_get,
            .set = [](int percent) { board_display_brightness_set(percent); },
            .follow = false,
        }));
        _pages.emplace_back(new VibrationPage());
        _pages.emplace_back(new StoragePage());
        _pages.emplace_back(new AboutPage());
    }

    Settings::~Settings()
    {
    }

    bool Settings::openWlan(int return_app_id)
    {
        Settings *settings = requestInstance();
        systems::base::Context *context = settings->getSystemContext();
        if (!context)
        {
            return false;
        }
        s_wlan_requested = true;
        s_wlan_return_app = return_app_id;
        s_wlan_connected_ticks = 0;
        systems::base::Context::AppEventData event = {
            .id = settings->getId(),
            .type = systems::base::Context::AppEventType::START,
            .data = nullptr,
        };
        return context->sendAppEvent(&event);
    }

    void Settings::openRequestedPage()
    {
        if (!s_wlan_requested || !_list_root)
        {
            return;
        }
        s_wlan_requested = false;
        if (_page != _pages[WLAN_PAGE].get())
        {
            closePage();
            openPage(WLAN_PAGE);
        }
    }

    /* Brings the app that asked for a network back once one is joined */
    void Settings::checkWlanReturn()
    {
        if (s_wlan_return_app < 0)
        {
            return;
        }
        wifi_service_info_t info = {};
        wifi_service_get_info(&info);
        if (info.state != WIFI_SERVICE_CONNECTED)
        {
            s_wlan_connected_ticks = 0;
            return;
        }
        if (++s_wlan_connected_ticks < RETURN_DELAY_TICKS)
        {
            return;
        }
        systems::base::Context::AppEventData event = {
            .id = s_wlan_return_app,
            .type = systems::base::Context::AppEventType::START,
            .data = nullptr,
        };
        s_wlan_return_app = -1;
        if (getSystemContext())
        {
            getSystemContext()->sendAppEvent(&event);
        }
    }

    bool Settings::run(void)
    {
        ESP_UTILS_LOGD("Run");
        lv_obj_t *screen = lv_scr_act();
        lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        _page = nullptr;
        _focus = 0;
        buildList();
        refreshValues();
        _timer = lv_timer_create(timer_cb, REFRESH_MS, this);
        knob_service_attach(knob_cb, this);
        openRequestedPage();
        return true;
    }

    void Settings::buildList()
    {
        _list_root = round_ui::create_plain(lv_scr_act());
        lv_obj_set_size(_list_root, round_ui::SCREEN, round_ui::SCREEN);
        lv_obj_t *title = round_ui::create_label(_list_root, &lv_font_montserrat_28, round_ui::COLOR_TEXT);
        lv_label_set_text(title, "Settings");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, round_ui::TITLE_Y + 4);

        _list = round_ui::create_column(_list_root);
        _items.clear();
        _values.clear();
        for (size_t i = 0; i < _pages.size(); i++)
        {
            round_ui::Row row = round_ui::create_row(_list, _pages[i]->icon(), _pages[i]->title(), " ", true);
            lv_obj_set_style_border_color(row.obj, lv_color_hex(round_ui::COLOR_ACCENT), LV_PART_MAIN);
            lv_obj_set_style_border_width(row.obj, 0, LV_PART_MAIN);
            lv_obj_set_user_data(row.obj, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
            lv_obj_add_event_cb(row.obj, item_cb, LV_EVENT_CLICKED, this);
            _items.push_back(row.obj);
            _values.push_back(row.value);
        }
    }

    void Settings::refreshValues()
    {
        for (size_t i = 0; i < _pages.size() && i < _values.size(); i++)
        {
            const std::string value = _pages[i]->summary();
            if (value != lv_label_get_text(_values[i]))
            {
                lv_label_set_text(_values[i], value.c_str());
            }
            lv_obj_remove_flag(_values[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    void Settings::focusItem(int index)
    {
        if (_items.empty())
        {
            return;
        }
        lv_obj_set_style_border_width(_items[_focus], 0, LV_PART_MAIN);
        _focus = std::clamp(index, 0, static_cast<int>(_items.size()) - 1);
        lv_obj_set_style_border_width(_items[_focus], 2, LV_PART_MAIN);
        lv_obj_scroll_to_view(_items[_focus], LV_ANIM_ON);
    }

    void Settings::openPage(int index)
    {
        if (_page || index < 0 || index >= static_cast<int>(_pages.size()))
        {
            return;
        }
        // A tap moves the knob focus too, so the outline must not stay on the old row
        const bool outlined = lv_obj_get_style_border_width(_items[_focus], LV_PART_MAIN) > 0;
        lv_obj_set_style_border_width(_items[_focus], 0, LV_PART_MAIN);
        _focus = index;
        lv_obj_set_style_border_width(_items[_focus], outlined ? 2 : 0, LV_PART_MAIN);
        lv_obj_add_flag(_list_root, LV_OBJ_FLAG_HIDDEN);
        _page_root = round_ui::create_plain(lv_scr_act());
        lv_obj_set_size(_page_root, round_ui::SCREEN, round_ui::SCREEN);
        _page = _pages[index].get();
        _page->open(_page_root);
        // Over the page's own objects, which may reach up to it, such as the arc of Sound
        round_ui::create_title(_page_root, _page->title(), title_cb, this);
    }

    void Settings::closePage()
    {
        if (!_page)
        {
            return;
        }
        _page->close();
        _page = nullptr;
        lv_obj_delete(_page_root);
        _page_root = nullptr;
        lv_obj_remove_flag(_list_root, LV_OBJ_FLAG_HIDDEN);
        refreshValues();
    }

    void Settings::goBack()
    {
        if (!_page)
        {
            notifyCoreClosed();
        }
        else if (!_page->back())
        {
            closePage();
        }
    }

    void Settings::item_cb(lv_event_t *e)
    {
        Settings *settings = static_cast<Settings *>(lv_event_get_user_data(e));
        lv_obj_t *item = lv_event_get_current_target_obj(e);
        settings->openPage(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(item))));
    }

    void Settings::title_cb(lv_event_t *e)
    {
        // The title belongs to the page that going back deletes
        lv_async_call(back_async, lv_event_get_user_data(e));
    }

    void Settings::back_async(void *user_data)
    {
        Settings *settings = static_cast<Settings *>(user_data);
        if (settings->_list_root)
        {
            settings->goBack();
        }
    }

    void Settings::timer_cb(lv_timer_t *timer)
    {
        Settings *settings = static_cast<Settings *>(lv_timer_get_user_data(timer));
        if (!settings->_page)
        {
            settings->refreshValues();
        }
        settings->checkWlanReturn();
    }

    void Settings::knob_cb(int steps, void *user_ctx)
    {
        Settings *settings = static_cast<Settings *>(user_ctx);
        if (!settings || !settings->_list_root)
        {
            return;
        }
        if (settings->_page)
        {
            settings->_page->knob(steps);
        }
        else
        {
            settings->focusItem(settings->_focus + steps);
        }
    }

    bool Settings::back(void)
    {
        ESP_UTILS_LOGD("Back");
        goBack();
        return true;
    }

    bool Settings::close(void)
    {
        ESP_UTILS_LOGD("Close");
        knob_service_detach(knob_cb, this);
        s_wlan_return_app = -1;
        if (_timer)
        {
            lv_timer_delete(_timer);
            _timer = nullptr;
        }
        if (_page)
        {
            _page->close();
            _page = nullptr;
        }
        // The objects go with the app's screen
        _list_root = _list = _page_root = nullptr;
        _items.clear();
        _values.clear();
        return true;
    }

    bool Settings::pause(void)
    {
        ESP_UTILS_LOGD("Pause");
        knob_service_detach(knob_cb, this);
        // Unless this pause is the request itself, leaving Settings drops the way back
        if (!s_wlan_requested)
        {
            s_wlan_return_app = -1;
        }
        if (_page)
        {
            _page->pause();
        }
        return true;
    }

    bool Settings::resume(void)
    {
        ESP_UTILS_LOGD("Resume");
        knob_service_attach(knob_cb, this);
        openRequestedPage();
        return true;
    }
} // namespace esp_brookesia::apps
