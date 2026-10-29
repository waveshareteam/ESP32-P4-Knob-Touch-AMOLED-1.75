/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:BtMusic"
#include "esp_lib_utils.h"
#include "app_fonts.h"
#include "audio_service.h"
#include "knob_service.h"
#include "BtMusic.hpp"

#define APP_NAME "BT Music"

LV_IMG_DECLARE(img_app_btmusic);

namespace esp_brookesia::apps {

static constexpr uint32_t REFRESH_PERIOD_MS = 200;
static constexpr int KNOB_VOLUME_STEP = 5;
static constexpr uint32_t PENDING_HOLD_MS = 3000;
static constexpr uint32_t PAIR_ACCENT = 0x4F8BFF;
static constexpr uint32_t BG_COLOR = 0x0B0B12;

BtMusic *BtMusic::_instance = nullptr;

BtMusic *BtMusic::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new BtMusic(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

BtMusic::BtMusic(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_btmusic, true, use_status_bar, use_navigation_bar)
{
}

BtMusic::~BtMusic()
{
    _view.destroy();
}

bool BtMusic::run(void)
{
    ESP_UTILS_LOGD("Run");

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    _view.create(screen, [this](NowPlayingView::Action action) {
        onAction(action);
    }, false);
    createPairView(screen);

    _shown_track_seq = UINT32_MAX;
    _shown_cover_seq = UINT32_MAX;
    _pending_active = false;
    refresh(true);

    _refresh_timer = lv_timer_create(refresh_timer_cb, REFRESH_PERIOD_MS, this);
    knob_service_attach(knob_cb, this);
    return true;
}

void BtMusic::createPairView(lv_obj_t *screen)
{
    _pair_view = lv_obj_create(screen);
    lv_obj_remove_style_all(_pair_view);
    lv_obj_remove_flag(_pair_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(_pair_view, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(_pair_view, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *badge = lv_obj_create(_pair_view);
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 120, 120);
    lv_obj_align(badge, LV_ALIGN_CENTER, 0, -70);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(PAIR_ACCENT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_t *icon = lv_label_create(badge);
    lv_label_set_text(icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_44, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);

    _pair_label = lv_label_create(_pair_view);
    lv_obj_set_width(_pair_label, 320);
    lv_label_set_long_mode(_pair_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_font(_pair_label, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_pair_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(_pair_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_pair_label, LV_ALIGN_CENTER, 0, 60);
}

void BtMusic::refresh(bool force)
{
    bt_service_get_info(&_info);
    const bool connected = _info.state == BT_STATE_CONNECTED || _info.state == BT_STATE_STREAMING;

    if (force || _info.state != _shown_state) {
        _shown_state = _info.state;
        _view.setVisible(connected);
        if (connected) {
            lv_obj_add_flag(_pair_view, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(_pair_view, LV_OBJ_FLAG_HIDDEN);
            if (_info.state == BT_STATE_OFF) {
                lv_label_set_text(_pair_label, "Bluetooth is off.\nTurn it on in Settings > Bluetooth.");
            } else {
                lv_label_set_text_fmt(_pair_label, "Open Bluetooth on your phone\nand connect to \"%s\".",
                                      _info.local_name);
            }
        }
    }
    if (!connected) {
        return;
    }

    if (force || _info.track_seq != _shown_track_seq) {
        _shown_track_seq = _info.track_seq;
        _view.setTitle(_info.title[0] ? _info.title : "Play music on your phone");
        char line[200];
        if (_info.artist[0] && _info.album[0]) {
            snprintf(line, sizeof(line), "%s  ·  %s", _info.artist, _info.album);
        } else {
            snprintf(line, sizeof(line), "%s", _info.artist[0] ? _info.artist : _info.peer_name);
        }
        _view.setSubtitle(line);
    }

    // Prefer the phone's play status: its audio stream stops seconds after a pause
    bool playing = _info.play_status_known ? _info.playing : (_info.state == BT_STATE_STREAMING);
    if (_pending_active) {
        if (playing == _pending_playing || lv_tick_elaps(_pending_since) > PENDING_HOLD_MS) {
            _pending_active = false;
        } else {
            playing = _pending_playing;
        }
    }
    _view.setPlaying(playing);
    _view.setTime(_info.position_ms, _info.duration_ms);

    if (force || _info.cover_seq != _shown_cover_seq) {
        _shown_cover_seq = _info.cover_seq;
        updateCover();
    }
}

void BtMusic::updateCover(void)
{
    uint8_t *jpeg = nullptr;
    size_t len = 0;
    if (bt_service_copy_cover(&jpeg, &len) != ESP_OK) {
        _view.clearCover();
        return;
    }
    // On a decode error the previous cover stays
    _view.setCoverJpeg(jpeg, len);
    free(jpeg);
}

void BtMusic::onAction(NowPlayingView::Action action)
{
    switch (action) {
    case NowPlayingView::Action::PREV:
        bt_service_prev();
        break;
    case NowPlayingView::Action::NEXT:
        bt_service_next();
        break;
    case NowPlayingView::Action::PLAY_PAUSE: {
        const bool play = !_view.isPlaying();
        if (play) {
            bt_service_play();
        } else {
            bt_service_pause();
        }
        // Show the new state right away; the phone confirms it shortly after
        _pending_active = true;
        _pending_playing = play;
        _pending_since = lv_tick_get();
        _view.setPlaying(play);
        break;
    }
    default:
        break;
    }
}

void BtMusic::refresh_timer_cb(lv_timer_t *timer)
{
    static_cast<BtMusic *>(lv_timer_get_user_data(timer))->refresh(false);
}

void BtMusic::knob_cb(int steps, void *user_ctx)
{
    BtMusic *app = static_cast<BtMusic *>(user_ctx);
    if (!app || !app->_view.root()) {
        return;
    }
    // The device volume; the phone's slider follows it
    const int volume = std::min(100, std::max(0, audio_service_get_volume() + steps * KNOB_VOLUME_STEP));
    audio_service_set_volume(volume);
    app->_view.showVolume(volume);
}

bool BtMusic::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool BtMusic::close(void)
{
    ESP_UTILS_LOGD("Close");
    knob_service_detach(knob_cb, this);
    if (_refresh_timer) {
        lv_timer_delete(_refresh_timer);
        _refresh_timer = nullptr;
    }
    // The core deletes the screen objects
    _view.destroy();
    _pair_view = _pair_label = nullptr;
    return true;
}

bool BtMusic::init(void)
{
    return true;
}

bool BtMusic::deinit(void)
{
    return true;
}

// In the background the phone keeps playing; only the knob goes back to the system
bool BtMusic::pause(void)
{
    knob_service_detach(knob_cb, this);
    return true;
}

bool BtMusic::resume(void)
{
    knob_service_attach(knob_cb, this);
    refresh(true);
    return true;
}

} // namespace esp_brookesia::apps
