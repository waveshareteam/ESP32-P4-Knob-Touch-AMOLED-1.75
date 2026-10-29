/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "bt_service.h"
#include "now_playing_view.hpp"

namespace esp_brookesia::apps {

/**
 * @brief Now-playing screen for music streamed from a phone over Bluetooth
 *
 * Shows the cover, the title line (lyrics when the phone's music app sends them as
 * the title), artist and progress, controls the phone's player and turns the knob
 * into a volume control.
 */
class BtMusic: public systems::phone::App {
public:
    static BtMusic *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~BtMusic();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    BtMusic(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static BtMusic *_instance;

    NowPlayingView _view;
    // Shown instead of the player while no phone is connected
    lv_obj_t *_pair_view = nullptr;
    lv_obj_t *_pair_label = nullptr;
    lv_timer_t *_refresh_timer = nullptr;

    bt_info_t _info = {};
    uint32_t _shown_track_seq = UINT32_MAX;
    uint32_t _shown_cover_seq = UINT32_MAX;
    bt_state_t _shown_state = BT_STATE_OFF;
    // A play/pause tap wins over stale phone state for a short while
    bool _pending_active = false;
    bool _pending_playing = false;
    uint32_t _pending_since = 0;

    void createPairView(lv_obj_t *screen);
    void refresh(bool force);
    void updateCover(void);
    void onAction(NowPlayingView::Action action);

    static void refresh_timer_cb(lv_timer_t *timer);
    static void knob_cb(int steps, void *user_ctx);
};

} // namespace esp_brookesia::apps
