/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include "lvgl.h"
#if __has_include("lvgl_private.h")
#   include "lvgl_private.h"
#else
#   include "src/lvgl_private.h"
#endif
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Gallery"
#include "esp_lib_utils.h"
#include "esp_heap_caps.h"
#include "app_fonts.h"
#include "board_app.h"
#include "knob_service.h"
#include "Gallery.hpp"

#define APP_NAME "Gallery"

LV_IMG_DECLARE(img_app_gallery);

namespace esp_brookesia::apps {

static constexpr int SCREEN = BOARD_LCD_H_RES;
static constexpr int WORK_MAX_SIDE = 1400;          // Panoramas keep some detail for the whole view
static constexpr int WHOLE_DIAMETER = SCREEN - 12;  // A whole picture stays clear of the bezel
static constexpr size_t MAX_FILES = 5000;
static constexpr int SCAN_DEPTH = 3;
static constexpr uint32_t REFRESH_MS = 40;
static constexpr uint32_t SPINNER_DELAY_MS = 200;
static constexpr uint32_t SLIDE_MS = 240;
static constexpr uint32_t FADE_MS = 160;
static constexpr uint32_t OVERLAY_NAV_MS = 1500;
static constexpr uint32_t OVERLAY_TAP_MS = 4000;
static constexpr uint32_t DOUBLE_TAP_MS = 280;
static constexpr uint32_t WORKER_STACK = 12 * 1024;
static constexpr UBaseType_t WORKER_PRIORITY = 2;
static constexpr uint32_t ACCENT_COLOR = 0x8C7BFF;
static constexpr uint32_t HINT_COLOR = 0xA0A3B1;

Gallery *Gallery::_instance = nullptr;

Gallery *Gallery::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Gallery(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Gallery::Gallery(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_gallery, true, use_status_bar, use_navigation_bar)
{
}

Gallery::~Gallery()
{
}

static lv_obj_t *create_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *create_pill(lv_obj_t *parent, lv_obj_t **label, const lv_font_t *font)
{
    lv_obj_t *pill = create_plain(parent);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(pill, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(pill, 6, LV_PART_MAIN);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_HIDDEN);
    *label = lv_label_create(pill);
    lv_obj_set_style_text_font(*label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(*label, lv_color_white(), LV_PART_MAIN);
    return pill;
}

static void scan_dir(const std::string &dir, int depth, std::vector<std::string> &files)
{
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return;
    }
    std::vector<std::string> subdirs;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr && files.size() < MAX_FILES) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (entry->d_type == DT_DIR) {
            if (depth < SCAN_DEPTH && strcasecmp(entry->d_name, "System Volume Information") != 0) {
                subdirs.push_back(dir + "/" + entry->d_name);
            }
        } else if (gallery_is_picture(entry->d_name)) {
            files.push_back(dir + "/" + entry->d_name);
        }
    }
    closedir(d);
    for (const std::string &sub : subdirs) {
        scan_dir(sub, depth + 1, files);
    }
}

static const char *base_name(const std::string &path)
{
    const size_t slash = path.rfind('/');
    return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

bool Gallery::init(void)
{
    _lock = xSemaphoreCreateMutex();
    _file_lock = xSemaphoreCreateMutex();
    _worker_done = xSemaphoreCreateBinary();
    ESP_UTILS_CHECK_FALSE_RETURN(_lock && _file_lock && _worker_done, false, "Create locks failed");
    xSemaphoreGive(_worker_done);
    return true;
}

bool Gallery::deinit(void)
{
    return true;
}

bool Gallery::run(void)
{
    ESP_UTILS_LOGD("Run");
    // A worker from the previous visit may still be finishing a large picture
    ESP_UTILS_CHECK_FALSE_RETURN(xSemaphoreTake(_worker_done, pdMS_TO_TICKS(5000)) == pdTRUE, false,
                                 "Previous worker still busy");

    xSemaphoreTake(_lock, portMAX_DELAY);
    _files.clear();
    _scanned = false;
    _scan_requested = true;
    _stop = false;
    _sd_released = !storage_sd_is_mounted();
    _wanted = 0;
    _ui_scan_gen = _scan_gen;
    xSemaphoreGive(_lock);
    _count = 0;
    _index = 0;
    _shown_index = _shown_mode = -1;
    _pending_dir = 0;
    _wait_since = 0;
    _overlay_ms = 0;

    buildUi();
    if (xTaskCreate(workerTask, "gallery", WORKER_STACK, this, WORKER_PRIORITY, &_worker) != pdPASS) {
        _worker = nullptr;
        xSemaphoreGive(_worker_done);
        ESP_UTILS_LOGE("Create worker failed");
        return false;
    }
    storage_add_listener(storage_event_cb, this);
    knob_service_attach(knob_cb, this);
    _timer = lv_timer_create(timer_cb, REFRESH_MS, this);
    refresh();
    return true;
}

void Gallery::buildUi(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Two pictures, so one can slide out while the next slides in
    for (int i = 0; i < 2; i++) {
        _images[i] = lv_image_create(screen);
        lv_obj_remove_flag(_images[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(_images[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(_images[i]);
        _image_slots[i] = nullptr;
    }
    _front = 0;

    _message = create_plain(screen);
    lv_obj_set_size(_message, 320, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(_message, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_message, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_message, 10, LV_PART_MAIN);
    lv_obj_center(_message);
    lv_obj_add_flag(_message, LV_OBJ_FLAG_HIDDEN);
    _message_icon = lv_label_create(_message);
    lv_obj_set_style_text_font(_message_icon, &lv_font_montserrat_44, LV_PART_MAIN);
    lv_obj_set_style_text_color(_message_icon, lv_color_hex(ACCENT_COLOR), LV_PART_MAIN);
    _message_title = lv_label_create(_message);
    lv_obj_set_style_text_font(_message_title, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(_message_title, lv_color_white(), LV_PART_MAIN);
    _message_hint = lv_label_create(_message);
    lv_obj_set_width(_message_hint, 300);
    lv_label_set_long_mode(_message_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(_message_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_message_hint, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_message_hint, lv_color_hex(HINT_COLOR), LV_PART_MAIN);

    _spinner = lv_spinner_create(screen);
    lv_obj_set_size(_spinner, 56, 56);
    lv_obj_center(_spinner);
    lv_obj_set_style_arc_width(_spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_spinner, lv_color_hex(0x2A2A35), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_spinner, lv_color_hex(ACCENT_COLOR), LV_PART_INDICATOR);
    lv_obj_remove_flag(_spinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);

    // Swipes and taps land on one layer above the pictures
    lv_obj_t *touch = create_plain(screen);
    lv_obj_set_size(touch, SCREEN, SCREEN);
    lv_obj_center(touch);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(touch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(touch, touch_event_cb, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(touch, touch_event_cb, LV_EVENT_SINGLE_CLICKED, this);
    lv_obj_add_event_cb(touch, touch_event_cb, LV_EVENT_DOUBLE_CLICKED, this);

    _counter_pill = create_pill(screen, &_counter, &lv_font_montserrat_18);
    lv_obj_align(_counter_pill, LV_ALIGN_TOP_MID, 0, 36);
    _name_pill = create_pill(screen, &_name, app_font_text_20());
    lv_obj_set_width(_name, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(_name, 260, LV_PART_MAIN);
    lv_label_set_long_mode(_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(_name_pill, LV_ALIGN_BOTTOM_MID, 0, -56);
}

int Gallery::wrapIndex(int index) const
{
    return _count > 0 ? ((index % _count) + _count) % _count : 0;
}

Gallery::Slot *Gallery::slotWithIndexLocked(int index)
{
    for (Slot &slot : _slots) {
        if (slot.index == index) {
            return &slot;
        }
    }
    return nullptr;
}

Gallery::Slot *Gallery::findSlot(int index)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    Slot *slot = slotWithIndexLocked(index);
    xSemaphoreGive(_lock);
    return slot;
}

void Gallery::wakeWorker(void)
{
    // Only while the worker is sure to be alive: it leaves only after seeing _stop
    xSemaphoreTake(_lock, portMAX_DELAY);
    if (_worker && !_stop) {
        xTaskNotifyGive(_worker);
    }
    xSemaphoreGive(_lock);
}

void Gallery::showMessage(const char *icon, const char *title, const char *hint)
{
    lv_label_set_text(_message_icon, icon);
    lv_label_set_text(_message_title, title);
    lv_label_set_text(_message_hint, hint ? hint : "");
    lv_obj_remove_flag(_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
}

void Gallery::showOverlay(uint32_t duration_ms)
{
    if (_count == 0) {
        return;
    }
    updateOverlay();
    lv_obj_remove_flag(_counter_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_name_pill, LV_OBJ_FLAG_HIDDEN);
    _overlay_since = lv_tick_get();
    _overlay_ms = duration_ms;
}

void Gallery::hideOverlay(void)
{
    lv_obj_add_flag(_counter_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_name_pill, LV_OBJ_FLAG_HIDDEN);
    _overlay_ms = 0;
}

void Gallery::updateOverlay(void)
{
    char text[24];
    snprintf(text, sizeof(text), "%d / %d", _index + 1, _count);
    lv_label_set_text(_counter, text);
    xSemaphoreTake(_lock, portMAX_DELAY);
    if (_index < (int)_files.size()) {
        lv_label_set_text(_name, base_name(_files[_index]));
    }
    xSemaphoreGive(_lock);
}

void Gallery::unpinImage(int which)
{
    lv_anim_delete(_images[which], nullptr);
    lv_obj_add_flag(_images[which], LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(_images[which], nullptr);
    lv_obj_set_style_opa(_images[which], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(_images[which], LV_ALIGN_CENTER, 0, 0);
    if (_image_slots[which]) {
        _image_slots[which]->pins--;
        _image_slots[which] = nullptr;
    }
}

void Gallery::clearImages(void)
{
    unpinImage(0);
    unpinImage(1);
    _shown_index = _shown_mode = -1;
}

void Gallery::releaseSlot(Slot *slot)
{
    if (slot->view) {
        lv_image_cache_drop(&slot->dsc);
        free(slot->view);
        slot->view = nullptr;
    }
    gallery_image_free(&slot->work);
    slot->dsc = {};
    slot->view_mode = -1;
    xSemaphoreTake(_lock, portMAX_DELAY);
    slot->index = -1;
    slot->failed = false;
    slot->reason = nullptr;
    xSemaphoreGive(_lock);
}

void Gallery::releaseAllSlots(void)
{
    clearImages();
    for (Slot &slot : _slots) {
        releaseSlot(&slot);
    }
}

/* Renders the working copy for the screen: a centre square that covers it, or the whole picture
 * inside the circle */
bool Gallery::renderView(Slot *slot)
{
    const gallery_image_t &work = slot->work;
    int sx = 0;
    int sy = 0;
    int sw = work.width;
    int sh = work.height;
    int vw;
    int vh;
    if (_mode == VIEW_FILL) {
        const float scale = std::max((float)SCREEN / work.width, (float)SCREEN / work.height);
        vw = vh = SCREEN;
        sw = std::min(work.width, std::max(1, (int)lroundf(SCREEN / scale)));
        sh = std::min(work.height, std::max(1, (int)lroundf(SCREEN / scale)));
        sx = (work.width - sw) / 2;
        sy = (work.height - sh) / 2;
    } else {
        const float aspect = (float)work.width / work.height;
        const float diagonal = sqrtf(1.0f + aspect * aspect);
        vw = std::max(1, (int)lroundf(WHOLE_DIAMETER * aspect / diagonal));
        vh = std::max(1, (int)lroundf(WHOLE_DIAMETER / diagonal));
    }
    uint16_t *pixels = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, (size_t)vw * vh * 2,
                                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels) {
        return false;
    }
    gallery_resample(&work, sx, sy, sw, sh, pixels, vw, vh);

    uint16_t *old = slot->view;
    if (old) {
        lv_image_cache_drop(&slot->dsc);
    }
    slot->view = pixels;
    slot->view_mode = _mode;
    slot->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    slot->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    slot->dsc.header.w = vw;
    slot->dsc.header.h = vh;
    slot->dsc.header.stride = vw * 2;
    slot->dsc.data_size = (uint32_t)vw * vh * 2;
    slot->dsc.data = reinterpret_cast<const uint8_t *>(pixels);
    for (int i = 0; i < 2; i++) {
        if (_image_slots[i] == slot) {
            lv_image_set_src(_images[i], nullptr);
            lv_image_set_src(_images[i], &slot->dsc);
        }
    }
    free(old);
    return true;
}

void Gallery::showSlot(Slot *slot, int dir)
{
    const int next = _front ^ 1;
    // A slide still running ends at once
    unpinImage(next);
    lv_obj_t *out = _images[_front];
    lv_obj_t *in = _images[next];
    lv_anim_delete(out, nullptr);
    lv_obj_align(out, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(out, LV_OPA_COVER, LV_PART_MAIN);

    lv_image_set_src(in, &slot->dsc);
    slot->pins++;
    _image_slots[next] = slot;
    lv_obj_remove_flag(in, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t anim;
    if (dir != 0 && _image_slots[_front]) {
        // The new picture pushes the old one off the side it was swiped towards
        lv_obj_align(in, LV_ALIGN_CENTER, dir * SCREEN, 0);
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, in);
        lv_anim_set_exec_cb(&anim, [](void *obj, int32_t v) {
            lv_obj_set_x(static_cast<lv_obj_t *>(obj), v);
        });
        lv_anim_set_values(&anim, dir * SCREEN, 0);
        lv_anim_set_duration(&anim, SLIDE_MS);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_start(&anim);

        lv_anim_set_var(&anim, out);
        lv_anim_set_values(&anim, 0, -dir * SCREEN);
        lv_anim_set_completed_cb(&anim, slide_done_cb);
        lv_anim_start(&anim);
    } else {
        unpinImage(_front);
        lv_obj_align(in, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_opa(in, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, in);
        lv_anim_set_exec_cb(&anim, [](void *obj, int32_t v) {
            lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
        });
        lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&anim, FADE_MS);
        lv_anim_start(&anim);
    }
    _front = next;
}

void Gallery::slide_done_cb(lv_anim_t *anim)
{
    Gallery *app = _instance;
    for (int i = 0; app && i < 2; i++) {
        if (app->_images[i] == anim->var && i != app->_front) {
            app->unpinImage(i);
        }
    }
}

void Gallery::refresh(void)
{
    xSemaphoreTake(_lock, portMAX_DELAY);
    const bool scanned = _scanned;
    const int scan_gen = _scan_gen;
    const int count = (int)_files.size();
    xSemaphoreGive(_lock);

    if (scan_gen != _ui_scan_gen) {
        // A new listing makes every decoded picture stale
        releaseAllSlots();
        _ui_scan_gen = scan_gen;
        _count = count;
        _index = 0;
        _pending_dir = 0;
        xSemaphoreTake(_lock, portMAX_DELAY);
        _wanted = 0;
        xSemaphoreGive(_lock);
        wakeWorker();
    }
    if (_overlay_ms && lv_tick_elaps(_overlay_since) > _overlay_ms) {
        hideOverlay();
    }

    const storage_sd_state_t sd = storage_sd_get_state();
    if (sd != STORAGE_SD_MOUNTED) {
        clearImages();
        hideOverlay();
        if (sd == STORAGE_SD_USB) {
            showMessage(LV_SYMBOL_USB, "Card in use", "The microSD card is open on a computer. Stop USB access in Settings to view pictures.");
        } else if (sd == STORAGE_SD_NO_CARD) {
            showMessage(LV_SYMBOL_SD_CARD, "No microSD card", "Insert a card with JPG or PNG pictures in the pictures folder.");
        } else {
            showMessage(LV_SYMBOL_WARNING, "Card unreadable", "Format the microSD card as FAT32 or exFAT.");
        }
        return;
    }
    if (!scanned) {
        lv_obj_add_flag(_message, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (count == 0) {
        clearImages();
        hideOverlay();
        showMessage(LV_SYMBOL_IMAGE, "No pictures", "Copy JPG or PNG files to the pictures folder on the microSD card.");
        return;
    }

    // Pictures far from the shown one make room for its neighbours
    const int near[] = {_index, wrapIndex(_index + 1), wrapIndex(_index - 1), wrapIndex(_index + 2)};
    bool freed = false;
    for (Slot &slot : _slots) {
        xSemaphoreTake(_lock, portMAX_DELAY);
        const int index = slot.index;
        xSemaphoreGive(_lock);
        if (index >= 0 && slot.pins == 0 && std::find(std::begin(near), std::end(near), index) == std::end(near)) {
            releaseSlot(&slot);
            freed = true;
        }
    }
    if (freed) {
        wakeWorker();
    }

    if (_shown_index == _index && _shown_mode == _mode) {
        return;
    }
    Slot *slot = findSlot(_index);
    if (!slot) {
        // Still decoding: a spinner only appears when it takes a moment
        if (_wait_since == 0) {
            _wait_since = lv_tick_get();
        }
        if (lv_tick_elaps(_wait_since) > SPINNER_DELAY_MS) {
            lv_obj_remove_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    _wait_since = 0;
    lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);

    if (slot->failed) {
        clearImages();
        showMessage(LV_SYMBOL_WARNING, "Can't show this picture", slot->reason);
    } else {
        lv_obj_add_flag(_message, LV_OBJ_FLAG_HIDDEN);
        const bool same_picture = _shown_index == _index && _image_slots[_front] == slot;
        if (slot->view_mode != _mode && !renderView(slot)) {
            clearImages();
            showMessage(LV_SYMBOL_WARNING, "Can't show this picture", "Not enough memory");
        } else if (!same_picture) {
            showSlot(slot, _pending_dir);
        }
    }
    _shown_index = _index;
    _shown_mode = _mode;
    _pending_dir = 0;
    if (_overlay_ms) {
        updateOverlay();
    }
}

void Gallery::navigate(int steps)
{
    if (_count == 0 || steps == 0) {
        return;
    }
    _index = wrapIndex(_index + steps);
    _pending_dir = steps > 0 ? 1 : -1;
    xSemaphoreTake(_lock, portMAX_DELAY);
    _wanted = _index;
    xSemaphoreGive(_lock);
    wakeWorker();
    // A tapped-open overlay stays; otherwise the position shows briefly
    showOverlay(_overlay_ms == OVERLAY_TAP_MS ? OVERLAY_TAP_MS : OVERLAY_NAV_MS);
    refresh();
}

void Gallery::timer_cb(lv_timer_t *timer)
{
    static_cast<Gallery *>(lv_timer_get_user_data(timer))->refresh();
}

void Gallery::tap_timer_cb(lv_timer_t *timer)
{
    Gallery *app = static_cast<Gallery *>(lv_timer_get_user_data(timer));
    app->_tap_timer = nullptr;
    if (app->_overlay_ms) {
        app->hideOverlay();
    } else {
        app->showOverlay(OVERLAY_TAP_MS);
    }
}

void Gallery::touch_event_cb(lv_event_t *e)
{
    Gallery *app = static_cast<Gallery *>(lv_event_get_user_data(e));
    switch (lv_event_get_code(e)) {
    case LV_EVENT_GESTURE: {
        lv_indev_t *indev = lv_indev_active();
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            app->navigate(dir == LV_DIR_LEFT ? 1 : -1);
            lv_indev_wait_release(indev);   // The swipe is not also a tap
        }
        break;
    }
    case LV_EVENT_SINGLE_CLICKED:
        // Wait to see whether a second tap follows
        if (app->_tap_timer) {
            lv_timer_delete(app->_tap_timer);
        }
        app->_tap_timer = lv_timer_create(tap_timer_cb, DOUBLE_TAP_MS, app);
        lv_timer_set_repeat_count(app->_tap_timer, 1);
        break;
    case LV_EVENT_DOUBLE_CLICKED:
        if (app->_tap_timer) {
            lv_timer_delete(app->_tap_timer);
            app->_tap_timer = nullptr;
        }
        app->_mode = (app->_mode == VIEW_FILL) ? VIEW_WHOLE : VIEW_FILL;
        app->refresh();
        break;
    default:
        break;
    }
}

void Gallery::knob_cb(int steps, void *user_ctx)
{
    Gallery *app = static_cast<Gallery *>(user_ctx);
    if (app && app->_timer) {
        app->navigate(steps);
    }
}

void Gallery::storage_event_cb(storage_event_t event, void *user_ctx)
{
    Gallery *app = static_cast<Gallery *>(user_ctx);
    if (event == STORAGE_EVENT_SD_RELEASE) {
        // Waits for a read in progress, then keeps the worker off the card
        xSemaphoreTake(app->_file_lock, portMAX_DELAY);
        xSemaphoreTake(app->_lock, portMAX_DELAY);
        app->_sd_released = true;
        xSemaphoreGive(app->_lock);
        xSemaphoreGive(app->_file_lock);
    } else if (event == STORAGE_EVENT_SD_MOUNTED) {
        xSemaphoreTake(app->_lock, portMAX_DELAY);
        app->_sd_released = false;
        app->_scan_requested = true;
        xSemaphoreGive(app->_lock);
        app->wakeWorker();
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Worker                                                                                     */
/* ------------------------------------------------------------------------------------------ */

void Gallery::workerTask(void *arg)
{
    Gallery *app = static_cast<Gallery *>(arg);
    if (gallery_decoder_init() != ESP_OK) {
        ESP_UTILS_LOGW("Hardware JPEG decoder unavailable; decoding in software");
    }
    app->workerLoop();
    gallery_decoder_deinit();
    xSemaphoreGive(app->_worker_done);
    vTaskDelete(nullptr);
}

void Gallery::readDone(void *ctx)
{
    xSemaphoreGive(static_cast<Gallery *>(ctx)->_file_lock);
}

void Gallery::workerLoop(void)
{
    static constexpr int ORDER[] = {0, 1, -1, 2};
    while (true) {
        xSemaphoreTake(_lock, portMAX_DELAY);
        if (_stop) {
            _worker = nullptr;
            xSemaphoreGive(_lock);
            return;
        }
        const bool scan = _scan_requested && !_sd_released;
        if (scan) {
            _scan_requested = false;
        }
        const int gen = _scan_gen;
        int job = -1;
        Slot *target = nullptr;
        std::string path;
        if (!scan && _scanned && !_sd_released && !_files.empty()) {
            const int count = (int)_files.size();
            for (int offset : ORDER) {
                const int index = (((_wanted + offset) % count) + count) % count;
                if (!slotWithIndexLocked(index)) {
                    job = index;
                    break;
                }
            }
            target = slotWithIndexLocked(-1);
            if (job >= 0 && target) {
                path = _files[job];
            } else {
                job = -1;
            }
        }
        xSemaphoreGive(_lock);

        if (scan) {
            std::vector<std::string> files;
            xSemaphoreTake(_file_lock, portMAX_DELAY);
            xSemaphoreTake(_lock, portMAX_DELAY);
            const bool released = _sd_released;
            xSemaphoreGive(_lock);
            if (!released) {
                scan_dir(STORAGE_DIR_PICTURES, 0, files);
            }
            xSemaphoreGive(_file_lock);
            std::sort(files.begin(), files.end(), [](const std::string & a, const std::string & b) {
                return strcasecmp(a.c_str(), b.c_str()) < 0;
            });
            ESP_UTILS_LOGI("Found %d pictures", (int)files.size());
            xSemaphoreTake(_lock, portMAX_DELAY);
            if (!_stop) {
                _files.swap(files);
                _scanned = true;
                _scan_gen++;
            }
            xSemaphoreGive(_lock);
            continue;
        }
        if (job < 0) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }

        xSemaphoreTake(_file_lock, portMAX_DELAY);
        xSemaphoreTake(_lock, portMAX_DELAY);
        const bool released = _sd_released;
        xSemaphoreGive(_lock);
        if (released) {
            xSemaphoreGive(_file_lock);
            continue;
        }
        gallery_image_t work = {};
        const char *reason = nullptr;
        // The card is free again as soon as the file is in memory
        const esp_err_t ret = gallery_decode_file(path.c_str(), SCREEN, WORK_MAX_SIDE, &work, &reason, readDone, this);

        xSemaphoreTake(_lock, portMAX_DELAY);
        const bool publish = !_stop && gen == _scan_gen && target->index == -1 && !slotWithIndexLocked(job);
        if (publish) {
            target->work = work;
            target->failed = ret != ESP_OK;
            target->reason = reason ? reason : "This picture can't be decoded";
            target->index = job;
        }
        xSemaphoreGive(_lock);
        if (!publish) {
            gallery_image_free(&work);
        }
    }
}

bool Gallery::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool Gallery::close(void)
{
    ESP_UTILS_LOGD("Close");
    knob_service_detach(knob_cb, this);
    storage_remove_listener(storage_event_cb, this);
    if (_tap_timer) {
        lv_timer_delete(_tap_timer);
        _tap_timer = nullptr;
    }
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    // The worker leaves after the picture it is decoding; nothing waits for it here
    xSemaphoreTake(_lock, portMAX_DELAY);
    _stop = true;
    if (_worker) {
        xTaskNotifyGive(_worker);
    }
    xSemaphoreGive(_lock);
    releaseAllSlots();

    for (lv_obj_t *&image : _images) {
        image = nullptr;
    }
    _spinner = _message = _message_icon = _message_title = _message_hint = nullptr;
    _counter_pill = _counter = _name_pill = _name = nullptr;
    return true;
}

bool Gallery::pause(void)
{
    knob_service_detach(knob_cb, this);
    return true;
}

bool Gallery::resume(void)
{
    knob_service_attach(knob_cb, this);
    return true;
}

} // namespace esp_brookesia::apps
