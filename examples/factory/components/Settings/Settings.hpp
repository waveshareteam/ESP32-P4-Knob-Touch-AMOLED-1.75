/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <memory>
#include <vector>
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "lvgl.h"
#include "ui/SettingsPage.hpp"

namespace esp_brookesia::apps
{

    /**
     * @brief Settings: a list of pages, each showing its current value
     *
     * The knob moves through the list and works the open page; the title at the top of a page,
     * or the system back, returns to the list.
     */
    class Settings : public systems::phone::App
    {
    public:
        static Settings *requestInstance(bool use_status_bar = true, bool use_navigation_bar = false);

        /**
         * @brief Bring Settings up on its WLAN page, for an app that needs a network
         *
         * Once a network is joined there, the app `return_app_id` is brought back; leaving
         * Settings before that forgets it. Call from the LVGL task.
         */
        static bool openWlan(int return_app_id);
        ~Settings();

    protected:
        Settings(bool use_status_bar, bool use_navigation_bar);

        bool run(void) override;
        bool back(void) override;
        bool close(void) override;
        bool pause(void) override;
        bool resume(void) override;

    private:
        static Settings *_instance;

        std::vector<std::unique_ptr<SettingsPage>> _pages;
        SettingsPage *_page = nullptr;      // Open page, nullptr on the list
        lv_obj_t *_list_root = nullptr;
        lv_obj_t *_list = nullptr;
        std::vector<lv_obj_t *> _items;
        std::vector<lv_obj_t *> _values;
        int _focus = 0;
        lv_obj_t *_page_root = nullptr;
        lv_timer_t *_timer = nullptr;

        void buildList();
        void refreshValues();
        void focusItem(int index);
        void openPage(int index);
        void openRequestedPage();
        void checkWlanReturn();
        void closePage();
        void goBack();

        static void item_cb(lv_event_t *e);
        static void title_cb(lv_event_t *e);
        static void back_async(void *user_data);
        static void timer_cb(lv_timer_t *timer);
        static void knob_cb(int steps, void *user_ctx);
    };

} // namespace esp_brookesia::apps
