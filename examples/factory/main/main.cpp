/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_brookesia.hpp"
#include "boost/thread.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"

#include "esp_ota_ops.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_extconn.h"

#include "board_app.h"
#include "audio_service.h"
#include "storage_service.h"
#include "knob_service.h"
#include "bt_service.h"
#include "haptic_service.h"
#include "power_service.h"
#include "time_service.h"
#include "wifi_service.h"

#include "Drawpanel.hpp"
#include "SpecAnalyzer.hpp"
#include "MusicPlayer.hpp"
#include "BtMusic.hpp"
#include "Pomodoro.hpp"
#include "Settings.hpp"
#include "VideoPlayer.hpp"
#include "Gallery.hpp"
#include "Reader.hpp"
#include "Motion.hpp"
#include "XiaozhiApp.hpp"
#include "UsbScreen.hpp"
#include "esp_brookesia_app_calculator.hpp"
#include "esp_heap_caps.h"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

constexpr bool EXAMPLE_SHOW_MEM_INFO = true;

extern "C" void app_main(void)
{
    esp_log_level_set("rpc_rsp", ESP_LOG_ERROR);
    const esp_partition_t *update_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL
    );
    ESP_UTILS_CHECK_NULL_EXIT(update_partition, "Factory partition not found");
    ESP_LOGI(ESP_UTILS_LOG_TAG, "Switch to partition factory");
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Board Manager mounts SPIFFS and brings up the display, touch and codec */
    ESP_ERROR_CHECK(board_app_init());

    ESP_ERROR_CHECK(board_audio_init());

    /* The vibration motor is optional feedback; the apps work without it */
    if (haptic_service_init() != ESP_OK) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "Vibration motor unavailable");
    }
    /* The clock is right before Wi-Fi connects when the RTC kept counting through a reset */
    if (time_service_init() != ESP_OK) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "RTC unavailable");
    }
    if (power_service_init() != ESP_OK) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "Battery state unavailable");
    }

    /* Apps share the codec through the audio service; it restores the saved volume */
    ESP_ERROR_CHECK(audio_service_init());

    /* Bluetooth and Wi-Fi run on the ESP32 co-processor, which must be up first */
    esp_extconn_config_t extconn_cfg = ESP_EXTCONN_CONFIG_DEFAULT();
    err = esp_extconn_init(&extconn_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(ESP_UTILS_LOG_TAG, "Wireless co-processor init failed: %s", esp_err_to_name(err));
    } else {
        if ((err = bt_service_init()) != ESP_OK) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Bluetooth init failed: %s", esp_err_to_name(err));
        }
        /* Joins the saved network again when Wi-Fi was left on */
        if ((err = wifi_service_init()) != ESP_OK) {
            ESP_LOGE(ESP_UTILS_LOG_TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
        }
    }

    /* The co-processor link enables the SDMMC IO supply, so mount the card after it */
    ESP_ERROR_CHECK(storage_service_init());

    ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");

    esp_lv_adapter_config_t lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();

    // Keep LVGL on core 1 with a larger stack; high-resolution UI and app switching can
    // otherwise starve the GUI task or overflow the default stack. The stack stays in internal
    // RAM: apps save settings to NVS from LVGL callbacks, and a flash write asserts when the
    // writing task's stack is in PSRAM.
    lv_adapter_cfg.task_stack_size = 16 * 1024;
    lv_adapter_cfg.task_priority = 10;
    lv_adapter_cfg.task_core_id = 1;
    lv_adapter_cfg.stack_in_psram = false;

    /* Configure display */
    lv_display_t *disp = board_display_start(&lv_adapter_cfg);
    ESP_UTILS_CHECK_NULL_EXIT(disp, "Start display failed");
    board_display_backlight_off();

    /* Knob steps are delivered under the LVGL lock, so start them once LVGL runs */
    ESP_ERROR_CHECK(knob_service_init());

    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        bool locked = board_display_lock(timeout_ms);
        ESP_UTILS_CHECK_FALSE_RETURN(locked, false, "Lock failed (timeout_ms: %d)", timeout_ms);

        return true;
    }, []() {
        board_display_unlock();
        return true;
    });

    /* Create a phone object */
    Phone *phone = new (std::nothrow) Phone();
    ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

    {
        // When operating on non-GUI tasks, should acquire a lock before operating on LVGL
        LvLockGuard gui_guard;

        /* Begin the phone */
        ESP_UTILS_CHECK_FALSE_EXIT(
            phone->addStylesheet(ESP_BROOKESIA_PHONE_466_466_DARK_STYLESHEET()),
            "Add 466x466 stylesheet failed"
        );
        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
        // assert(phone->getDisplay().showContainerBorder() && "Show container border failed");

        /* Init and install apps from registry */
        std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
        ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(inited_apps), "Init app registry failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(inited_apps), "Install app registry failed");

        auto app2 = esp_brookesia::apps::Calculator::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(app2), "Start Calculator failed");
        auto app = esp_brookesia::apps::Drawpanel::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(app), "Start Drawpanel failed");

        auto app1 = esp_brookesia::apps::SpecAnalyzer::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(app1), "Start SpecAnalyzer failed");
        auto app5 = esp_brookesia::apps::MusicPlayer::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(app5), "Start MusicPlayer failed");
        auto bt_music_app = esp_brookesia::apps::BtMusic::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(bt_music_app), "Start BT Music failed");
        auto pomodoro_app = esp_brookesia::apps::Pomodoro::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(pomodoro_app), "Start Pomodoro failed");
        auto video_player_app = esp_brookesia::apps::VideoPlayer::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(video_player_app), "Start VideoPlayer failed");
        auto gallery_app = esp_brookesia::apps::Gallery::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(gallery_app), "Start Gallery failed");
        auto reader_app = esp_brookesia::apps::Reader::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(reader_app), "Start Reader failed");
        auto motion_app = esp_brookesia::apps::Motion::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(motion_app), "Start Motion failed");
        auto xiaozhi_app = esp_brookesia::apps::XiaozhiApp::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(xiaozhi_app), "Start Xiaozhi failed");
        auto usb_screen_app = esp_brookesia::apps::UsbScreen::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(usb_screen_app), "Start USB Screen failed");
        auto app6 = esp_brookesia::apps::Settings::requestInstance();
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installApp(app6), "Start Settings failed");

        board_display_backlight_on();

        /* Create a timer to update the clock */
        lv_timer_create([](lv_timer_t *t) {
            static constexpr int BATTERY_PERIOD_S = 10;
            static int battery_countdown = 0;
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");
            StatusBar *status_bar = phone->getDisplay().getStatusBar();

            if (time_service_is_valid()) {
                time(&now);
                localtime_r(&now, &timeinfo);
                ESP_UTILS_CHECK_FALSE_EXIT(status_bar->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                                           "Refresh status bar failed");
            } else {
                status_bar->clearClock();
            }

            // Without a battery the board runs from USB and shows no battery icon
            if (--battery_countdown <= 0) {
                battery_countdown = BATTERY_PERIOD_S;
                power_battery_t battery;
                if (power_service_get_battery(&battery) == ESP_OK && battery.present) {
                    status_bar->setBatteryPercent(battery.charging, battery.percent);
                } else {
                    status_bar->hideBatteryIcon();
                }
            }

            wifi_service_info_t wifi;
            wifi_service_get_info(&wifi);
            StatusBar::WifiState wifi_state = StatusBar::WifiState::DISCONNECTED;
            if (wifi.state == WIFI_SERVICE_CONNECTED) {
                wifi_state = (wifi.rssi > -60) ? StatusBar::WifiState::SIGNAL_3 :
                             (wifi.rssi > -72) ? StatusBar::WifiState::SIGNAL_2 : StatusBar::WifiState::SIGNAL_1;
            }
            ESP_UTILS_CHECK_FALSE_EXIT(status_bar->setWifiIconState(wifi_state), "Refresh status bar failed");
        }, 1000, phone);
    }

    if constexpr (EXAMPLE_SHOW_MEM_INFO) {
        esp_utils::thread_config_guard thread_config({
            .name = "mem_info",
            .stack_size = 4096,
        });
        boost::thread([ = ]() {
            char buffer[128];    /* Make sure buffer is enough for `sprintf` */
            size_t internal_free = 0;
            size_t internal_total = 0;
            size_t external_free = 0;
            size_t external_total = 0;

            while (1) {
                internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
                external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
                snprintf(
                    buffer,
                    sizeof(buffer),
                    "\t           Biggest /     Free /    Total\n"
                    "\t  SRAM : [%8zu / %8zu / %8zu]\n"
                    "\t PSRAM : [%8zu / %8zu / %8zu]",
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), internal_free, internal_total,
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free, external_total
                );
                ESP_UTILS_LOGI("\n%s", buffer);

                {
                    LvLockGuard gui_guard;
                    ESP_UTILS_CHECK_FALSE_EXIT(
                        phone->getDisplay().getRecentsScreen()->setMemoryLabel(
                            internal_free / 1024, internal_total / 1024, external_free / 1024, external_total / 1024
                        ), "Set memory label failed"
                    );
                }

                boost::this_thread::sleep_for(boost::chrono::seconds(5));
            }
        }).detach();
    }
}
