/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "storage_service.h"
#include "gallery_decoder.h"

namespace esp_brookesia::apps {

/**
 * @brief Picture viewer for JPEG and PNG files in /sdcard/pictures
 *
 * A worker task scans the folder and decodes the shown picture first, then its neighbours, so
 * swiping or turning the knob usually finds the next picture ready. A tap shows the file name,
 * a double tap switches between filling the round screen and showing the whole picture.
 */
class Gallery: public systems::phone::App {
public:
    static Gallery *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~Gallery();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    Gallery(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static constexpr int SLOT_COUNT = 4;

    enum ViewMode {
        VIEW_FILL,      // Covers the round screen, edges cropped
        VIEW_WHOLE,     // The whole picture inside the circle
    };

    /* One decoded file. The worker only fills free slots; the UI renders and frees them. */
    struct Slot {
        int index = -1;             // File index, -1 when free
        bool failed = false;
        const char *reason = nullptr;
        gallery_image_t work = {};  // Decoded working copy
        uint16_t *view = nullptr;   // Rendered for the screen in view_mode
        lv_image_dsc_t dsc = {};
        int view_mode = -1;
        int pins = 0;               // Shown on screen; never freed while pinned
    };

    static Gallery *_instance;

    // Shared with the worker, guarded by _lock
    SemaphoreHandle_t _lock = nullptr;
    SemaphoreHandle_t _file_lock = nullptr;     // Held while the worker reads the card
    SemaphoreHandle_t _worker_done = nullptr;
    TaskHandle_t _worker = nullptr;
    std::vector<std::string> _files;
    Slot _slots[SLOT_COUNT];
    int _scan_gen = 0;
    bool _scanned = false;
    bool _scan_requested = false;
    bool _stop = false;
    bool _sd_released = false;
    int _wanted = 0;

    // UI only
    lv_obj_t *_images[2] = {};
    Slot *_image_slots[2] = {};
    int _front = 0;
    lv_obj_t *_spinner = nullptr;
    lv_obj_t *_message = nullptr;
    lv_obj_t *_message_icon = nullptr;
    lv_obj_t *_message_title = nullptr;
    lv_obj_t *_message_hint = nullptr;
    lv_obj_t *_counter_pill = nullptr;
    lv_obj_t *_counter = nullptr;
    lv_obj_t *_name_pill = nullptr;
    lv_obj_t *_name = nullptr;
    lv_timer_t *_timer = nullptr;
    lv_timer_t *_tap_timer = nullptr;
    int _ui_scan_gen = -1;
    int _count = 0;
    int _index = 0;
    int _shown_index = -1;
    int _shown_mode = -1;
    int _pending_dir = 0;
    int _mode = VIEW_FILL;
    uint32_t _wait_since = 0;
    uint32_t _overlay_since = 0;
    uint32_t _overlay_ms = 0;       // 0 while the overlay is hidden

    void buildUi(void);
    void refresh(void);
    void navigate(int steps);
    void showOverlay(uint32_t duration_ms);
    void hideOverlay(void);
    void updateOverlay(void);
    void wakeWorker(void);
    void showMessage(const char *icon, const char *title, const char *hint);
    void showSlot(Slot *slot, int dir);
    bool renderView(Slot *slot);
    void releaseSlot(Slot *slot);
    void releaseAllSlots(void);
    void clearImages(void);
    void unpinImage(int which);
    Slot *findSlot(int index);
    int wrapIndex(int index) const;

    static void workerTask(void *arg);
    static void readDone(void *ctx);
    void workerLoop(void);
    Slot *slotWithIndexLocked(int index);

    static void timer_cb(lv_timer_t *timer);
    static void tap_timer_cb(lv_timer_t *timer);
    static void touch_event_cb(lv_event_t *e);
    static void knob_cb(int steps, void *user_ctx);
    static void storage_event_cb(storage_event_t event, void *user_ctx);
    static void slide_done_cb(lv_anim_t *anim);
};

} // namespace esp_brookesia::apps
