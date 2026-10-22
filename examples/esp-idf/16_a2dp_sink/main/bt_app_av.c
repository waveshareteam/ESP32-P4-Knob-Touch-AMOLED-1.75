/*  
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD  
 *  
 * SPDX-License-Identifier: Unlicense OR CC0-1.0  
 */  
  
#include <stdint.h>  
#include <stdbool.h>  
#include <stdlib.h>  
#include <string.h>  
#include <inttypes.h>  
#include "esp_log.h"  
#include "esp_err.h"
#include "esp_timer.h"
  
#include "bt_app_core.h"  
#include "bt_app_av.h"  
#include "audio_mono_mix.h"
#include "bsp_board_extra.h"  
#include "esp_bt_main.h"  
#include "esp_bt_device.h"  
#include "esp_gap_bt_api.h"  
#include "esp_a2dp_api.h"  
#include "esp_avrc_api.h"  
  
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"  
#include "driver/gpio.h"
  
#include "sys/lock.h"  
  
#include "ui.h"  
  
/* AVRCP used transaction labels */  
#define APP_RC_CT_TL_GET_CAPS            (0)  
#define APP_RC_CT_TL_GET_META_DATA       (1)  
#define APP_RC_CT_TL_RN_TRACK_CHANGE     (2)  
#define APP_RC_CT_TL_RN_PLAYBACK_CHANGE  (3)  
#define APP_RC_CT_TL_RN_PLAY_POS_CHANGE  (4)  
  
/* Application layer causes delay value */  
#define APP_DELAY_VALUE 50 /* 5ms */  
  
/* ================= 按键配置区 ================= */  
static const char *TAG_VOL_BTN = "ROTARY_VOLUME";  
  
/* 行程开关 GPIO */  
#define BTN_INC_GPIO          20   /* 顺时针 -> 音量增加 */  
#define BTN_DEC_GPIO          21   /* 逆时针 -> 音量减小 */  
#define BUTTON_POLL_INTERVAL_MS 10
#define BUTTON_TASK_STACK_SIZE  3072
#define BUTTON_TASK_PRIORITY    4
  
/* 音量范围（与蓝牙音量映射：0x7F 对应 100%） */  
#define VOLUME_MIN            0  
#define VOLUME_MAX            100  
  
/* 每次按键音量变化步进（0x7F = 127 级，步进 5 约 4%） */  
#define VOLUME_STEP           5
/* 合并机械触点抖动和快速旋转脉冲，保持在人耳/手感难以察觉的窗口内 */
#define VOLUME_COALESCE_MS    80
#define VOLUME_NOTIFY_COALESCE_MS 160
#define CODEC_FIXED_OUTPUT_VOLUME 100
/* ==================================================== */  
  
/*******************************  
 * STATIC FUNCTION DECLARATIONS  
 ******************************/  
  
/* allocate new meta buffer */  
static void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param);  
/* handler for new track is loaded */  
static void bt_av_new_track(void);  
/* handler for track status change */  
static void bt_av_playback_changed(void);  
/* handler for track playing position change */  
static void bt_av_play_pos_changed(void);  
/* notification event handler */  
static void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter);  
/* installation for i2s */  
static esp_err_t bt_i2s_driver_install(void);  
/* uninstallation for i2s */  
static void bt_i2s_driver_uninstall(void);  
/* apply negotiated A2DP audio format to codec */  
static esp_err_t bt_i2s_apply_audio_cfg(void);
/* keep PA muted unless A2DP is actively streaming */
static void bt_i2s_sync_mute_state(void);
/* set volume by remote controller */  
static void volume_set_by_controller(uint8_t volume);  
/* set volume by local host */  
static void volume_set_by_local_host(uint8_t volume);  
/* a2dp event handler */  
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param);  
/* avrc controller event handler */  
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param);  
/* avrc target event handler */  
static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param);  
  
/* 按键相关声明 */  
static void volume_change(int delta);
static void volume_apply_pending(uint16_t event, void *p_param);
static void volume_notify_changed(uint16_t event, void *p_param);
static void volume_apply_timer_cb(void *arg);
static void volume_notify_timer_cb(void *arg);
static void volume_timers_init(void);
static void volume_request(int delta);
static void button_poll_task(void *arg);
static void button_init(void);  
  
/*******************************  
 * STATIC VARIABLE DEFINITIONS  
 ******************************/  
  
static uint32_t s_pkt_cnt = 0;                                              /* count for audio packet */  
static esp_a2d_audio_state_t s_audio_state = ESP_A2D_AUDIO_STATE_STOPPED;   /* audio stream datapath state */  
static const char *s_a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};  
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};  
static esp_avrc_rn_evt_cap_mask_t s_avrc_peer_rn_cap;                       /* AVRC target notification capability bit mask */  
static _lock_t s_volume_lock;
static _lock_t s_volume_pending_lock;
static uint8_t s_volume = 0x7F;    /* local volume value (0x00-0x7F), start at 100% */
static int s_volume_pending_delta;
static bool s_volume_work_pending;
static bool s_volume_notify;       /* notify volume change or not */
static bool s_volume_notify_work_pending;
static uint8_t s_volume_notify_pending_volume;
static esp_timer_handle_t s_volume_apply_timer;
static esp_timer_handle_t s_volume_notify_timer;
static bool s_codec_ready;  
static uint32_t s_audio_sample_rate = 44100;
static i2s_slot_mode_t s_audio_channel_mode = I2S_SLOT_MODE_STEREO;
  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
static bool cover_art_connected = false;  
static bool cover_art_getting = false;  
static uint32_t cover_art_image_size = 0;  
static uint8_t image_handle_old[7];  
#endif  
  
/********************************  
 * STATIC FUNCTION DEFINITIONS  
 *******************************/  
  
static void bt_app_alloc_meta_buffer(esp_avrc_ct_cb_param_t *param)  
{  
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);  
    uint8_t *attr_text = (uint8_t *)malloc(rc->meta_rsp.attr_length + 1);  
  
    memcpy(attr_text, rc->meta_rsp.attr_text, rc->meta_rsp.attr_length);  
    attr_text[rc->meta_rsp.attr_length] = 0;  
    rc->meta_rsp.attr_text = attr_text;  
}  
  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
static bool image_handle_check(uint8_t *image_handle, int len)  
{  
    /* Image handle length must be 7 */  
    if (len == 7 && memcmp(image_handle_old, image_handle, 7) != 0) {  
        memcpy(image_handle_old, image_handle, 7);  
        return true;  
    }  
    return false;  
}  
#endif  
  
static void bt_av_new_track(void)  
{  
    /* request metadata */  
    uint8_t attr_mask = ESP_AVRC_MD_ATTR_TITLE |  
                        ESP_AVRC_MD_ATTR_ARTIST |  
                        ESP_AVRC_MD_ATTR_ALBUM |  
                        ESP_AVRC_MD_ATTR_GENRE;  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
    if (cover_art_connected) {  
        attr_mask |= ESP_AVRC_MD_ATTR_COVER_ART;  
    }  
#endif  
    esp_avrc_ct_send_metadata_cmd(APP_RC_CT_TL_GET_META_DATA, attr_mask);  
  
    /* register notification if peer support the event_id */  
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,  
                                           ESP_AVRC_RN_TRACK_CHANGE)) {  
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_TRACK_CHANGE,  
                                                   ESP_AVRC_RN_TRACK_CHANGE, 0);  
    }  
}  

static char *bt_av_copy_metadata_text(const esp_avrc_ct_cb_param_t *rc)
{
    const uint8_t *src = rc->meta_rsp.attr_text;
    uint16_t len = rc->meta_rsp.attr_length;

    if (!src || len == 0 || src[0] == '\0') {
        return NULL;
    }

    char *text = (char *)malloc(len + 1);
    if (!text) {
        return NULL;
    }

    memcpy(text, src, len);
    text[len] = '\0';
    return text;
}
  
static void bt_av_playback_changed(void)  
{  
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,  
                                           ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {  
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAYBACK_CHANGE,  
                                                   ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);  
    }  
}  
  
static void bt_av_play_pos_changed(void)  
{  
    if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &s_avrc_peer_rn_cap,  
                                           ESP_AVRC_RN_PLAY_POS_CHANGED)) {  
        esp_avrc_ct_send_register_notification_cmd(APP_RC_CT_TL_RN_PLAY_POS_CHANGE,  
                                                   ESP_AVRC_RN_PLAY_POS_CHANGED, 10);  
    }  
}  
  
static void bt_av_notify_evt_handler(uint8_t event_id, esp_avrc_rn_param_t *event_parameter)  
{  
    switch (event_id) {  
    case ESP_AVRC_RN_TRACK_CHANGE:  
        bt_av_new_track();  
        break;  
    case ESP_AVRC_RN_PLAY_STATUS_CHANGE:  
        ESP_LOGI(BT_AV_TAG, "Playback status changed: 0x%x", event_parameter->playback);  
        bt_av_playback_changed();  
        break;  
    case ESP_AVRC_RN_PLAY_POS_CHANGED:  
        ESP_LOGI(BT_AV_TAG, "Play position changed: %" PRIu32 "-ms", event_parameter->play_pos);  
        bt_av_play_pos_changed();  
        break;  
    default:  
        ESP_LOGI(BT_AV_TAG, "unhandled event: %d", event_id);  
        break;  
    }  
}  
  
static esp_err_t bt_i2s_driver_install(void)  
{  
    esp_err_t ret = bsp_extra_codec_init();
    if (ret != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "Codec init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = bt_i2s_apply_audio_cfg();
    if (ret != ESP_OK) {
        ESP_LOGE(BT_AV_TAG, "Codec format apply failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_codec_ready = true;  

    uint8_t volume;
    _lock_acquire(&s_volume_lock);
    volume = s_volume;
    _lock_release(&s_volume_lock);

    bt_i2s_set_pcm_volume(volume);

    ret = bsp_extra_codec_volume_set(CODEC_FIXED_OUTPUT_VOLUME, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(BT_AV_TAG, "Codec fixed volume set failed: %s", esp_err_to_name(ret));
    }

    ret = bsp_extra_codec_mute_set(true);
    if (ret != ESP_OK) {
        ESP_LOGW(BT_AV_TAG, "Codec initial mute failed: %s", esp_err_to_name(ret));
    }

    return ESP_OK;
}  

static esp_err_t bt_i2s_apply_audio_cfg(void)
{
    bool downmix_enabled = (s_audio_channel_mode == I2S_SLOT_MODE_STEREO);
    audio_stereo_mix_mode_t mix_mode = AUDIO_STEREO_MIX_LEFT_DUPLICATE;

#if CONFIG_EXAMPLE_A2DP_AUDIO_OUTPUT_RIGHT_DUPLICATE
    mix_mode = AUDIO_STEREO_MIX_RIGHT_DUPLICATE;
#elif CONFIG_EXAMPLE_A2DP_AUDIO_OUTPUT_MONO_AVERAGE
    mix_mode = AUDIO_STEREO_MIX_MONO_AVERAGE;
#elif CONFIG_EXAMPLE_A2DP_AUDIO_OUTPUT_STEREO_PASSTHROUGH
    mix_mode = AUDIO_STEREO_MIX_STEREO_PASSTHROUGH;
#endif
    audio_mono_mix_set_mode(mix_mode);

    ESP_LOGI(BT_AV_TAG, "Apply audio format: sample rate=%" PRIu32 ", channel=%s",
             s_audio_sample_rate,
             (s_audio_channel_mode == I2S_SLOT_MODE_MONO) ? "mono" : "stereo");
    ESP_LOGI(BT_AV_TAG, "Stereo output mix mode: %s", downmix_enabled ? audio_mono_mix_mode_name() : "disabled");
    bt_i2s_set_stereo_downmix_enabled(downmix_enabled);
    return bsp_extra_codec_set_fs(s_audio_sample_rate, 16, s_audio_channel_mode);
}

static void bt_i2s_sync_mute_state(void)
{
    if (!s_codec_ready) {
        return;
    }

    bool mute = (s_audio_state != ESP_A2D_AUDIO_STATE_STARTED);
    esp_err_t ret = bsp_extra_codec_mute_set(mute);
    if (ret != ESP_OK) {
        ESP_LOGW(BT_AV_TAG, "Codec %s failed: %s", mute ? "mute" : "unmute", esp_err_to_name(ret));
    }
}
  
static void bt_i2s_driver_uninstall(void)  
{  
    if (s_codec_ready) {
        ESP_ERROR_CHECK(bsp_extra_codec_dev_stop());  
        s_codec_ready = false;
    }
}  
  
static void volume_set_by_controller(uint8_t volume)  
{  
    uint32_t vol_percent = (uint32_t)volume * 100 / 0x7F;  
    ESP_LOGI(BT_RC_TG_TAG, "Volume is set by remote controller to: %" PRIu32 "%%", vol_percent);  
  
    _lock_acquire(&s_volume_lock);
    s_volume = volume;
    _lock_release(&s_volume_lock);

    bt_i2s_set_pcm_volume(volume);
}  
  
static void volume_set_by_local_host(uint8_t volume)  
{  
    uint32_t vol_percent = (uint32_t)volume * 100 / 0x7F;  
    ESP_LOGI(BT_RC_TG_TAG, "Volume is set locally to: %" PRIu32 "%%", vol_percent);  
  
    bool start_notify_timer = false;  
    _lock_acquire(&s_volume_lock);  
    s_volume = volume;  
    if (s_volume_notify) {  
        s_volume_notify_pending_volume = volume;  
        if (!s_volume_notify_work_pending) {  
            s_volume_notify_work_pending = true;  
            start_notify_timer = true;  
        }  
    }  
    _lock_release(&s_volume_lock);  
  
    bt_i2s_set_pcm_volume(volume);
  
    if (start_notify_timer) {  
        esp_err_t ret = esp_timer_start_once(s_volume_notify_timer, VOLUME_NOTIFY_COALESCE_MS * 1000);  
        if (ret != ESP_OK) {  
            _lock_acquire(&s_volume_lock);  
            s_volume_notify_work_pending = false;  
            _lock_release(&s_volume_lock);  
            ESP_LOGW(BT_RC_TG_TAG, "Volume notify timer start failed: %s", esp_err_to_name(ret));  
        }  
    }  
}  

static void volume_notify_changed(uint16_t event, void *p_param)
{
    (void)event;
    (void)p_param;

    bool notify;
    uint8_t volume;

    _lock_acquire(&s_volume_lock);
    notify = s_volume_notify;
    volume = s_volume_notify_pending_volume;
    s_volume_notify_work_pending = false;
    if (notify) {
        s_volume_notify = false;
    }
    _lock_release(&s_volume_lock);

    if (notify) {
        esp_avrc_rn_param_t rn_param;
        rn_param.volume = volume;
        esp_avrc_tg_send_rn_rsp(
            ESP_AVRC_RN_VOLUME_CHANGE,
            ESP_AVRC_RN_RSP_CHANGED,
            &rn_param
        );
    }
}

static void volume_apply_timer_cb(void *arg)
{
    (void)arg;

    if (!bt_app_work_dispatch(volume_apply_pending, 0, NULL, 0, NULL)) {
        _lock_acquire(&s_volume_pending_lock);
        s_volume_work_pending = false;
        _lock_release(&s_volume_pending_lock);
        ESP_LOGW(TAG_VOL_BTN, "Volume work dispatch failed");
    }
}

static void volume_notify_timer_cb(void *arg)
{
    (void)arg;

    if (!bt_app_work_dispatch(volume_notify_changed, 0, NULL, 0, NULL)) {
        _lock_acquire(&s_volume_lock);
        s_volume_notify_work_pending = false;
        _lock_release(&s_volume_lock);
        ESP_LOGW(BT_RC_TG_TAG, "Volume notify dispatch failed");
    }
}

static void volume_timers_init(void)
{
    if (s_volume_apply_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = volume_apply_timer_cb,
            .name = "vol_apply",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_volume_apply_timer));
    }

    if (s_volume_notify_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = volume_notify_timer_cb,
            .name = "vol_notify",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_volume_notify_timer));
    }
}
  
static void bt_av_hdl_a2d_evt(uint16_t event, void *p_param)  
{  
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);  
  
    esp_a2d_cb_param_t *a2d = NULL;  
  
    switch (event) {  
    case ESP_A2D_CONNECTION_STATE_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        uint8_t *bda = a2d->conn_stat.remote_bda;  
        ESP_LOGI(BT_AV_TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",  
                 s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);  
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {  
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);  
            bt_i2s_driver_uninstall();  
            bt_i2s_task_shut_down();  
        } else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {  
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);  
            if (bt_i2s_driver_install() == ESP_OK) {
                bt_i2s_task_start_up();
            }
        }  
        break;  
    }  
    case ESP_A2D_AUDIO_STATE_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        ESP_LOGI(BT_AV_TAG, "A2DP audio state: %s", s_a2d_audio_state_str[a2d->audio_stat.state]);  
        s_audio_state = a2d->audio_stat.state;  
        if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {  
            s_pkt_cnt = 0;  
        }  
        bt_i2s_sync_mute_state();
        break;  
    }  
    case ESP_A2D_AUDIO_CFG_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        esp_a2d_mcc_t *p_mcc = &a2d->audio_cfg.mcc;  
        ESP_LOGI(BT_AV_TAG, "A2DP audio stream configuration, codec type: %d", p_mcc->type);  
        if (p_mcc->type == ESP_A2D_MCT_SBC) {  
            int sample_rate = 16000;  
            int ch_count = 2;  
            if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K) {  
                sample_rate = 32000;  
            } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K) {  
                sample_rate = 44100;  
            } else if (p_mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K) {  
                sample_rate = 48000;  
            }  
            if (p_mcc->cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) {  
                ch_count = 1;  
            }  
            s_audio_sample_rate = sample_rate;
            s_audio_channel_mode = (ch_count == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
            if (s_codec_ready) {
                esp_err_t ret = bt_i2s_apply_audio_cfg();
                if (ret != ESP_OK) {
                    ESP_LOGE(BT_AV_TAG, "Configure audio player failed: %s", esp_err_to_name(ret));
                } else {
                    bt_i2s_sync_mute_state();
                }
            }
            ESP_LOGI(BT_AV_TAG, "Configure audio player: 0x%x-0x%x-0x%x-0x%x-0x%x-%d-%d",  
                     p_mcc->cie.sbc_info.samp_freq,  
                     p_mcc->cie.sbc_info.ch_mode,  
                     p_mcc->cie.sbc_info.block_len,  
                     p_mcc->cie.sbc_info.num_subbands,  
                     p_mcc->cie.sbc_info.alloc_mthd,  
                     p_mcc->cie.sbc_info.min_bitpool,  
                     p_mcc->cie.sbc_info.max_bitpool);  
            ESP_LOGI(BT_AV_TAG, "Audio player configured, sample rate: %d, channel: %d", sample_rate, ch_count);
        }  
        break;  
    }  
    case ESP_A2D_PROF_STATE_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state) {  
            ESP_LOGI(BT_AV_TAG, "A2DP PROF STATE: Init Complete");  
        } else {  
            ESP_LOGI(BT_AV_TAG, "A2DP PROF STATE: Deinit Complete");  
        }  
        break;  
    }  
    case ESP_A2D_SEP_REG_STATE_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        if (a2d->a2d_sep_reg_stat.reg_state == ESP_A2D_SEP_REG_SUCCESS) {  
            ESP_LOGI(BT_AV_TAG, "A2DP register SEP success, seid: %d", a2d->a2d_sep_reg_stat.seid);  
        } else {  
            ESP_LOGI(BT_AV_TAG, "A2DP register SEP fail, seid: %d, state: %d",  
                     a2d->a2d_sep_reg_stat.seid, a2d->a2d_sep_reg_stat.reg_state);  
        }  
        break;  
    }  
    case ESP_A2D_SNK_PSC_CFG_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        ESP_LOGI(BT_AV_TAG, "protocol service capabilities configured: 0x%x", a2d->a2d_psc_cfg_stat.psc_mask);  
        if (a2d->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT) {  
            ESP_LOGI(BT_AV_TAG, "Peer device support delay reporting");  
        } else {  
            ESP_LOGI(BT_AV_TAG, "Peer device unsupported delay reporting");  
        }  
        break;  
    }  
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        if (ESP_A2D_SET_INVALID_PARAMS == a2d->a2d_set_delay_value_stat.set_state) {  
            ESP_LOGI(BT_AV_TAG, "Set delay report value: fail");  
        } else {  
            ESP_LOGI(BT_AV_TAG, "Set delay report value: success, delay_value: %u * 1/10 ms",  
                     a2d->a2d_set_delay_value_stat.delay_value);  
        }  
        break;  
    }  
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:  
    {  
        a2d = (esp_a2d_cb_param_t *)(p_param);  
        ESP_LOGI(BT_AV_TAG, "Get delay report value: delay_value: %u * 1/10 ms",  
                 a2d->a2d_get_delay_value_stat.delay_value);  
        esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + APP_DELAY_VALUE);  
        break;  
    }  
    default:  
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);  
        break;  
    }  
}  
  
static void bt_av_hdl_avrc_ct_evt(uint16_t event, void *p_param)  
{  
    ESP_LOGD(BT_RC_CT_TAG, "%s event: %d", __func__, event);  
  
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(p_param);  
  
    switch (event) {  
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:  
    {  
        uint8_t *bda = rc->conn_stat.remote_bda;  
        ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",  
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);  
        if (rc->conn_stat.connected) {  
            esp_avrc_ct_send_get_rn_capabilities_cmd(APP_RC_CT_TL_GET_CAPS);  
        } else {  
            s_avrc_peer_rn_cap.bits = 0;  
        }  
        break;  
    }  
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:  
    {  
        ESP_LOGI(BT_RC_CT_TAG, "AVRC passthrough rsp: key_code 0x%x, key_state %d, rsp_code %d",  
                 rc->psth_rsp.key_code, rc->psth_rsp.key_state, rc->psth_rsp.rsp_code);  
        break;  
    }  
    case ESP_AVRC_CT_METADATA_RSP_EVT:  
    {  
        uint8_t attr_id = rc->meta_rsp.attr_id;  
        char *text = bt_av_copy_metadata_text(rc);
  
        if (!text) {
            ESP_LOGW(BT_RC_CT_TAG, "AVRC metadata rsp: attr 0x%x empty", attr_id);  
            free(rc->meta_rsp.attr_text);  
            break;  
        }  
  
        ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: attribute id 0x%x, %s", attr_id, text);  
  
        switch (attr_id) {  
        case ESP_AVRC_MD_ATTR_TITLE:
            ESP_LOGI(BT_RC_CT_TAG, "Title received: %s", text);
            ui_update_title(text);  
            break;  
        case ESP_AVRC_MD_ATTR_ARTIST:
            ESP_LOGI(BT_RC_CT_TAG, "Artist received: %s", text);  
            ui_update_lyrics(text);  
            break;  
        default:  
            ESP_LOGI(BT_RC_CT_TAG, "→ Unhandled metadata attr id 0x%x", attr_id);  
            break;  
        }  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
        if (rc->meta_rsp.attr_id == 0x80 && cover_art_connected && cover_art_getting == false) {  
            if (image_handle_check(rc->meta_rsp.attr_text, rc->meta_rsp.attr_length)) {  
                esp_avrc_ct_cover_art_get_linked_thumbnail(rc->meta_rsp.attr_text);  
                cover_art_getting = true;  
            }  
        }  
#endif  
        free(text);
        free(rc->meta_rsp.attr_text);  
        break;  
    }  
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:  
    {  
        ESP_LOGI(BT_RC_CT_TAG, "AVRC event notification: %d", rc->change_ntf.event_id);  
        bt_av_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);  
        break;  
    }  
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:  
    {  
        ESP_LOGI(BT_RC_CT_TAG, "AVRC remote features %" PRIx32 ", TG features %x",  
                 rc->rmt_feats.feat_mask, rc->rmt_feats.tg_feat_flag);  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
        if ((rc->rmt_feats.tg_feat_flag & ESP_AVRC_FEAT_FLAG_TG_COVER_ART) && !cover_art_connected) {  
            ESP_LOGW(BT_RC_CT_TAG, "Peer support Cover Art feature, start connection...");  
            esp_avrc_ct_cover_art_connect(0);  
        }  
#endif  
        break;  
    }  
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:  
    {  
        ESP_LOGI(BT_RC_CT_TAG, "remote rn_cap: count %d, bitmask 0x%x",  
                 rc->get_rn_caps_rsp.cap_count, rc->get_rn_caps_rsp.evt_set.bits);  
        s_avrc_peer_rn_cap.bits = rc->get_rn_caps_rsp.evt_set.bits;  
        bt_av_new_track();  
        bt_av_playback_changed();  
        bt_av_play_pos_changed();  
        break;  
    }  
    case ESP_AVRC_CT_COVER_ART_STATE_EVT:  
    {  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
        if (rc->cover_art_state.state == ESP_AVRC_COVER_ART_CONNECTED) {  
            cover_art_connected = true;  
            ESP_LOGW(BT_RC_CT_TAG, "Cover Art Client connected");  
        } else {  
            cover_art_connected = false;  
            ESP_LOGW(BT_RC_CT_TAG, "Cover Art Client disconnected, reason:%d", rc->cover_art_state.reason);  
        }  
#endif  
        break;  
    }  
    case ESP_AVRC_CT_COVER_ART_DATA_EVT:  
    {  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
        if (rc->cover_art_data.final) {  
            if (rc->cover_art_data.status == ESP_BT_STATUS_SUCCESS) {  
                ESP_LOGI(BT_RC_CT_TAG, "Cover Art Client final data event, image size: %lu bytes",  
                         cover_art_image_size);  
            } else {  
                ESP_LOGE(BT_RC_CT_TAG, "Cover Art Client get operation failed");  
            }  
            cover_art_image_size = 0;  
            cover_art_getting = false;  
        }  
#endif  
        break;  
    }  
    case ESP_AVRC_CT_PROF_STATE_EVT:  
    {  
        if (ESP_AVRC_INIT_SUCCESS == rc->avrc_ct_init_stat.state) {  
            ESP_LOGI(BT_RC_CT_TAG, "AVRCP CT STATE: Init Complete");  
        } else if (ESP_AVRC_DEINIT_SUCCESS == rc->avrc_ct_init_stat.state) {  
            ESP_LOGI(BT_RC_CT_TAG, "AVRCP CT STATE: Deinit Complete");  
        } else {  
            ESP_LOGE(BT_RC_CT_TAG, "AVRCP CT STATE error: %d", rc->avrc_ct_init_stat.state);  
        }  
        break;  
    }  
    default:  
        ESP_LOGE(BT_RC_CT_TAG, "%s unhandled event: %d", __func__, event);  
        break;  
    }  
}  
  
static void bt_av_hdl_avrc_tg_evt(uint16_t event, void *p_param)  
{  
    ESP_LOGD(BT_RC_TG_TAG, "%s event: %d", __func__, event);  
  
    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)(p_param);  
  
    switch (event) {  
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:  
    {  
        uint8_t *bda = rc->conn_stat.remote_bda;  
        ESP_LOGI(BT_RC_TG_TAG, "AVRC conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",  
                 rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);  
        break;  
    }  
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:  
    {  
        ESP_LOGI(BT_RC_TG_TAG, "AVRC passthrough cmd: key_code 0x%x, key_state %d",  
                 rc->psth_cmd.key_code, rc->psth_cmd.key_state);  
        break;  
    }  
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:  
    {  
        ESP_LOGI(BT_RC_TG_TAG, "AVRC set absolute volume: %d%%",  
                 (int)rc->set_abs_vol.volume * 100 / 0x7f);  
        volume_set_by_controller(rc->set_abs_vol.volume);  
        break;  
    }  
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:  
    {  
        ESP_LOGI(BT_RC_TG_TAG, "AVRC register event notification: %d, param: 0x%" PRIx32,  
                 rc->reg_ntf.event_id, rc->reg_ntf.event_parameter);  
        if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {  
            /* 在锁保护下设置 s_volume_notify 并读取当前音量 */  
            _lock_acquire(&s_volume_lock);  
            s_volume_notify = true;  
            uint8_t cur_volume = s_volume;  
            _lock_release(&s_volume_lock);  
  
            esp_avrc_rn_param_t rn_param;  
            rn_param.volume = cur_volume;  
            esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);  
        }  
        break;  
    }  
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:  
    {  
        ESP_LOGI(BT_RC_TG_TAG, "AVRC remote features: %" PRIx32 ", CT features: %x",  
                 rc->rmt_feats.feat_mask, rc->rmt_feats.ct_feat_flag);  
        break;  
    }  
    case ESP_AVRC_TG_PROF_STATE_EVT:  
    {  
        if (ESP_AVRC_INIT_SUCCESS == rc->avrc_tg_init_stat.state) {  
            ESP_LOGI(BT_RC_TG_TAG, "AVRCP TG STATE: Init Complete");  
        } else if (ESP_AVRC_DEINIT_SUCCESS == rc->avrc_tg_init_stat.state) {  
            ESP_LOGI(BT_RC_TG_TAG, "AVRCP TG STATE: Deinit Complete");  
        } else {  
            ESP_LOGE(BT_RC_TG_TAG, "AVRCP TG STATE error: %d", rc->avrc_tg_init_stat.state);  
        }  
        break;  
    }  
    default:  
        ESP_LOGE(BT_RC_TG_TAG, "%s unhandled event: %d", __func__, event);  
        break;  
    }  
}  
  
/* ================= 按键音量控制实现 ================= */  
  
/**  
 * @brief 按步进修改本地蓝牙音量  
 *  
 * @param delta  变化量（可为负数），单位为蓝牙音量级别 (0x00~0x7F)  
 */  
static void volume_change(int delta)  
{  
    uint8_t old_volume;  
    int new_volume;  
  
    /* 在锁保护下读取当前音量并计算新值 */  
    _lock_acquire(&s_volume_lock);  
    old_volume = s_volume;  
    new_volume = (int)old_volume + delta;  
    _lock_release(&s_volume_lock);  
  
    /* 限制蓝牙音量范围 0x00 ~ 0x7F */  
    if (new_volume > 0x7F) {  
        new_volume = 0x7F;  
    }  
    if (new_volume < 0x00) {  
        new_volume = 0x00;  
    }  
  
    /* 音量没有变化则直接返回 */  
    if ((uint8_t)new_volume == old_volume) {  
        return;  
    }  
  
    int old_percent = (old_volume * 100) / 0x7F;  
    int new_percent = (new_volume * 100) / 0x7F;  
  
    ESP_LOGI(TAG_VOL_BTN,  
             "Volume: %d%% -> %d%% (0x%02X -> 0x%02X)",  
             old_percent, new_percent,  
             old_volume, new_volume);  
  
    volume_set_by_local_host((uint8_t)new_volume);
}

static void volume_apply_pending(uint16_t event, void *p_param)
{
    (void)event;
    (void)p_param;

    int delta;
    _lock_acquire(&s_volume_pending_lock);
    delta = s_volume_pending_delta;
    s_volume_pending_delta = 0;
    s_volume_work_pending = false;
    _lock_release(&s_volume_pending_lock);

    if (delta != 0) {
        volume_change(delta);
    }
}

static void volume_request(int delta)
{
    bool dispatch = false;

    _lock_acquire(&s_volume_pending_lock);
    s_volume_pending_delta += delta;
    if (s_volume_pending_delta > 0x7F) {
        s_volume_pending_delta = 0x7F;
    } else if (s_volume_pending_delta < -0x7F) {
        s_volume_pending_delta = -0x7F;
    }
    if (!s_volume_work_pending) {
        s_volume_work_pending = true;
        dispatch = true;
    }
    _lock_release(&s_volume_pending_lock);

    if (dispatch) {
        esp_err_t ret = esp_timer_start_once(s_volume_apply_timer, VOLUME_COALESCE_MS * 1000);
        if (ret != ESP_OK) {
            _lock_acquire(&s_volume_pending_lock);
            s_volume_work_pending = false;
            _lock_release(&s_volume_pending_lock);
            ESP_LOGW(TAG_VOL_BTN, "Volume timer start failed: %s", esp_err_to_name(ret));
        }
    }
}

static void button_poll_task(void *arg)
{
    (void)arg;
    int inc_level = gpio_get_level(BTN_INC_GPIO);
    int dec_level = gpio_get_level(BTN_DEC_GPIO);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        int level = gpio_get_level(BTN_INC_GPIO);
        if (level != inc_level) {
            if (inc_level == 1 && level == 0) {
                volume_request(+VOLUME_STEP);
            }
            inc_level = level;
        }

        level = gpio_get_level(BTN_DEC_GPIO);
        if (level != dec_level) {
            if (dec_level == 1 && level == 0) {
                volume_request(-VOLUME_STEP);
            }
            dec_level = level;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(BUTTON_POLL_INTERVAL_MS));
    }
}

static void button_init(void)
{
    const gpio_config_t gpio_cfg = {
        .pin_bit_mask = (1ULL << BTN_INC_GPIO) | (1ULL << BTN_DEC_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    BaseType_t task_created = xTaskCreate(button_poll_task, "rotary_gpio",
                                          BUTTON_TASK_STACK_SIZE, NULL,
                                          BUTTON_TASK_PRIORITY, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG_VOL_BTN, "Failed to create rotary GPIO polling task");
        abort();
    }

    ESP_LOGI(TAG_VOL_BTN,
             "GPIO%d = Volume +, GPIO%d = Volume -, polling=%d ms, interrupts=off, pullup=on",
             BTN_INC_GPIO, BTN_DEC_GPIO, BUTTON_POLL_INTERVAL_MS);
}
  
/********************************  
 * EXTERNAL FUNCTION DEFINITIONS  
 *******************************/  
  
void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)  
{  
    switch (event) {  
    case ESP_A2D_CONNECTION_STATE_EVT:  
    case ESP_A2D_AUDIO_STATE_EVT:  
    case ESP_A2D_AUDIO_CFG_EVT:  
    case ESP_A2D_PROF_STATE_EVT:  
    case ESP_A2D_SEP_REG_STATE_EVT:  
    case ESP_A2D_SNK_PSC_CFG_EVT:  
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:  
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:  
    {  
        bt_app_work_dispatch(bt_av_hdl_a2d_evt, event, param, sizeof(esp_a2d_cb_param_t), NULL);  
        break;  
    }  
    default:  
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);  
        break;  
    }  
}  
  
#if CONFIG_EXAMPLE_A2DP_SINK_USE_EXTERNAL_CODEC == FALSE  
  
void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)  
{  
    if (s_audio_state != ESP_A2D_AUDIO_STATE_STARTED) {
        return;
    }

    write_ringbuf(data, len);  
  
    if (++s_pkt_cnt % 100 == 0) {  
        ESP_LOGI(BT_AV_TAG, "Audio packet count: %" PRIu32, s_pkt_cnt);  
    }  
}  
  
#else  
  
void bt_app_a2d_audio_data_cb(esp_a2d_conn_hdl_t conn_hdl, esp_a2d_audio_buff_t *audio_buf)  
{  
    ESP_LOGI(BT_AV_TAG, "data_len: %d, number_frame: %d, ts: %lu",  
             audio_buf->data_len, audio_buf->number_frame, audio_buf->timestamp);  
    esp_a2d_audio_buff_free(audio_buf);  
}  
  
#endif  
  
void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)  
{  
#if CONFIG_EXAMPLE_AVRCP_CT_COVER_ART_ENABLE  
    if (event == ESP_AVRC_CT_COVER_ART_DATA_EVT && param->cover_art_data.status == ESP_BT_STATUS_SUCCESS) {  
        cover_art_image_size += param->cover_art_data.data_len;  
    }  
#endif  
    switch (event) {  
    case ESP_AVRC_CT_METADATA_RSP_EVT:  
        bt_app_alloc_meta_buffer(param);  
        /* fall through */  
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:  
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:  
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:  
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:  
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:  
    case ESP_AVRC_CT_COVER_ART_STATE_EVT:  
    case ESP_AVRC_CT_COVER_ART_DATA_EVT:  
    case ESP_AVRC_CT_PROF_STATE_EVT:  
    {  
        bt_app_work_dispatch(bt_av_hdl_avrc_ct_evt, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);  
        break;  
    }  
    default:  
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);  
        break;  
    }  
}  
  
void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)  
{  
    switch (event) {  
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:  
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:  
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:  
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:  
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:  
    case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT:  
    case ESP_AVRC_TG_PROF_STATE_EVT:  
        bt_app_work_dispatch(bt_av_hdl_avrc_tg_evt, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL);  
        break;  
    default:  
        ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);  
        break;  
    }  
}  
  
/**  
 * @brief 蓝牙 AV 扩展初始化：锁、默认音量、按键  
 *  
 * 修复要点：  
 *  1. 先初始化锁  
 *  2. 再设置默认音量（只调用一次 volume_set_by_local_host）  
 *  3. 最后初始化按键（按键回调会用到锁和音量）  
 */  
void bt_app_av_init_ext(void)  
{  
    /* 1. 初始化音量锁 */  
    _lock_init(&s_volume_lock);
    _lock_init(&s_volume_pending_lock);
    volume_timers_init();
    s_volume_notify = false;
    s_volume_notify_work_pending = false;
    s_volume_pending_delta = 0;
    s_volume_work_pending = false;
    /* 2. 设置默认音量 100%（0x7F），仅通过一个入口设置 */  
    uint8_t default_vol = (uint8_t)(VOLUME_MAX * 0x7F / 100);  
    s_volume = default_vol;
    s_volume_notify_pending_volume = default_vol;
    bt_i2s_set_pcm_volume(default_vol);
    /* 注意：此时蓝牙尚未连接，只设置硬件音量，不触发 AVRCP 通知 */  
    uint32_t vol_percent = (uint32_t)default_vol * 100 / 0x7F;  
    ESP_LOGI(TAG_VOL_BTN, "Initial volume = %d%% (0x%02X)", (int)vol_percent, default_vol);  
  
    /* 3. 初始化按键（回调中会使用 s_volume_lock） */  
    button_init();  
}  
