#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/esp32_p4_knob_touch_amoled_1_75.h"
#include "sdkconfig.h"

static const char *TAG = "07_limit_switches";

#define LIMIT_SWITCH_1_GPIO GPIO_NUM_20
#define LIMIT_SWITCH_2_GPIO GPIO_NUM_21

#define LIMIT_SWITCHES_DISPLAY_ENABLED true
#if CONFIG_LIMIT_SWITCHES_BIAS_PULLUP
#define SWITCH_PULLUP true
#else
#define SWITCH_PULLUP false
#endif

#if CONFIG_LIMIT_SWITCHES_BIAS_PULLDOWN
#define SWITCH_PULLDOWN true
#else
#define SWITCH_PULLDOWN false
#endif

#ifdef CONFIG_SPIRAM_XIP_FROM_PSRAM
#define LIMIT_SWITCHES_PSRAM_XIP_ENABLED 1
#else
#define LIMIT_SWITCHES_PSRAM_XIP_ENABLED 0
#endif

typedef struct {
    const char *name;
    gpio_num_t gpio;
    int stable_level;
    uint32_t change_count;
    uint32_t raw_edge_count;
    atomic_int ui_level;
    atomic_uint_least32_t ui_change_count;
    int displayed_level;
    uint32_t displayed_change_count;
    uint32_t debounce_until_ms;
    pcnt_unit_handle_t pcnt_unit;
    pcnt_channel_handle_t pcnt_channel;
    int pcnt_last_count;
    lv_obj_t *panel;
    lv_obj_t *level_label;
    lv_obj_t *count_label;
} switch_state_t;

typedef struct {
    switch_state_t *switches;
    size_t count;
} ui_context_t;

static lv_display_t *g_disp;
static lv_obj_t *g_status_label;
static ui_context_t g_ui_context;

#define STATUS_LOG_INTERVAL_MS 1000
#define SWITCH_POLL_INTERVAL_MS 10
#define DISPLAY_REFRESH_INTERVAL_MS 100
#define LVGL_TICK_PERIOD_MS 10
#define LVGL_TASK_MIN_DELAY_MS 20
#define LVGL_TASK_MAX_DELAY_MS 50
#define SWITCH_PCNT_GLITCH_FILTER_NS 1000
#define SWITCH_PCNT_HIGH_LIMIT 30000

#if CONFIG_FREERTOS_CORETIMER_SYSTIMER_LVL3
#define FREERTOS_SYSTIMER_LEVEL 3
#else
#define FREERTOS_SYSTIMER_LEVEL 1
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static const char *level_text(int level)
{
    return level ? "HIGH" : "LOW";
}

static lv_color_t level_color(int level)
{
    return level ? lv_color_hex(0xC7362F) : lv_color_hex(0x178F45);
}

static void publish_switch_ui_state(switch_state_t *sw)
{
    atomic_store_explicit(&sw->ui_level, sw->stable_level, memory_order_relaxed);
    atomic_store_explicit(&sw->ui_change_count, sw->change_count, memory_order_relaxed);
}

static void set_label_text(lv_obj_t *label, const char *text)
{
    if (label) {
        lv_label_set_text(label, text);
    }
}

static void update_switch_panel(switch_state_t *sw, int level, uint32_t change_count)
{
    if (level != sw->displayed_level) {
        lv_label_set_text(sw->level_label, level_text(level));
        lv_obj_set_style_text_color(sw->level_label, level_color(level), LV_PART_MAIN);
        sw->displayed_level = level;
    }

    if (change_count != sw->displayed_change_count) {
        char text[48];
        snprintf(text, sizeof(text), "Triggers: %lu", (unsigned long)change_count);
        lv_label_set_text(sw->count_label, text);
        sw->displayed_change_count = change_count;
    }
}

static void ui_refresh_timer_cb(lv_timer_t *timer)
{
    ui_context_t *ctx = lv_timer_get_user_data(timer);

    /* The adapter invokes LVGL timers while holding its recursive LVGL lock. */
    for (size_t i = 0; i < ctx->count; ++i) {
        switch_state_t *sw = &ctx->switches[i];
        int level = atomic_load_explicit(&sw->ui_level, memory_order_relaxed);
        uint32_t change_count =
            atomic_load_explicit(&sw->ui_change_count, memory_order_relaxed);

        if (level != sw->displayed_level || change_count != sw->displayed_change_count) {
            update_switch_panel(sw, level, change_count);
        }
    }
}

static void create_switch_panel(lv_obj_t *parent, switch_state_t *sw)
{
    sw->panel = lv_obj_create(parent);
    lv_obj_set_size(sw->panel, 200, 160);
    lv_obj_set_style_radius(sw->panel, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw->panel, lv_color_hex(0x24303B), LV_PART_MAIN);
    lv_obj_set_style_border_width(sw->panel, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw->panel, lv_color_hex(0x1F2933), LV_PART_MAIN);
    lv_obj_set_style_pad_all(sw->panel, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw->panel, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *name = lv_label_create(sw->panel);
    lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
    lv_label_set_text(name, sw->name);
    lv_obj_set_width(name, 176);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_color(name, lv_color_white(), LV_PART_MAIN);

    sw->level_label = lv_label_create(sw->panel);
    lv_obj_set_style_text_font(sw->level_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_align(sw->level_label, LV_ALIGN_CENTER, 0, -4);

    sw->count_label = lv_label_create(sw->panel);
    lv_label_set_long_mode(sw->count_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(sw->count_label, 176);
    lv_obj_set_style_text_color(sw->count_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(sw->count_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    sw->displayed_level = -1;
    sw->displayed_change_count = UINT32_MAX;
    update_switch_panel(sw, sw->stable_level, sw->change_count);
}

static void create_ui(switch_state_t switches[], size_t count)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF4F7FA), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Limit Switches");
    lv_obj_set_style_text_color(title, lv_color_hex(0x17202A), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    g_status_label = lv_label_create(screen);
    lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(g_status_label, 426);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(0x3B4652), LV_PART_MAIN);
    lv_obj_align(g_status_label, LV_ALIGN_TOP_LEFT, 20, 50);
    set_label_text(g_status_label, "Falling-edge trigger counters");

    lv_obj_t *row = lv_obj_create(screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 430, 176);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 16);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (size_t i = 0; i < count; ++i) {
        create_switch_panel(row, &switches[i]);
    }
}

static esp_err_t switch_gpio_input(gpio_num_t gpio, bool pullup, bool pulldown)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

static esp_err_t switch_init(switch_state_t *sw)
{
    esp_err_t ret = switch_gpio_input(sw->gpio, SWITCH_PULLUP, SWITCH_PULLDOWN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL configure %s GPIO%d: %s", sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    pcnt_unit_config_t unit_cfg = {
        .low_limit = -1,
        .high_limit = SWITCH_PCNT_HIGH_LIMIT,
    };
    ret = pcnt_new_unit(&unit_cfg, &sw->pcnt_unit);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL create %s GPIO%d PCNT unit: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    pcnt_glitch_filter_config_t filter_cfg = {
        .max_glitch_ns = SWITCH_PCNT_GLITCH_FILTER_NS,
    };
    ret = pcnt_unit_set_glitch_filter(sw->pcnt_unit, &filter_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL configure %s GPIO%d PCNT glitch filter: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    pcnt_chan_config_t channel_cfg = {
        .edge_gpio_num = sw->gpio,
        .level_gpio_num = -1,
    };
    ret = pcnt_new_channel(sw->pcnt_unit, &channel_cfg, &sw->pcnt_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL create %s GPIO%d PCNT channel: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = pcnt_channel_set_edge_action(sw->pcnt_channel,
                                       PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                       PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL set %s GPIO%d PCNT edge action: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = pcnt_channel_set_level_action(sw->pcnt_channel,
                                        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                        PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL set %s GPIO%d PCNT level action: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    ret = pcnt_unit_enable(sw->pcnt_unit);
    if (ret == ESP_OK) {
        ret = pcnt_unit_clear_count(sw->pcnt_unit);
    }
    if (ret == ESP_OK) {
        ret = pcnt_unit_start(sw->pcnt_unit);
    }
    if (ret == ESP_OK) {
        ret = pcnt_unit_get_count(sw->pcnt_unit, &sw->pcnt_last_count);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL start %s GPIO%d PCNT unit: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    sw->stable_level = gpio_get_level(sw->gpio);
    ESP_LOGI(TAG, "%s GPIO%d initial %s", sw->name, sw->gpio, level_text(sw->stable_level));
    return ESP_OK;
}

static esp_err_t switch_poll(switch_state_t *sw, uint32_t now)
{
    int count;
    esp_err_t ret = pcnt_unit_get_count(sw->pcnt_unit, &count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FAIL read %s GPIO%d PCNT count: %s",
                 sw->name, sw->gpio, esp_err_to_name(ret));
        return ret;
    }

    sw->stable_level = gpio_get_level(sw->gpio);

    uint32_t delta;
    if (count >= sw->pcnt_last_count) {
        delta = (uint32_t)(count - sw->pcnt_last_count);
    } else {
        delta = (uint32_t)(SWITCH_PCNT_HIGH_LIMIT - sw->pcnt_last_count + count);
    }
    sw->pcnt_last_count = count;

    if (delta == 0) {
        return ESP_OK;
    }

    sw->raw_edge_count += delta;
    if ((int32_t)(now - sw->debounce_until_ms) >= 0) {
        sw->change_count++;
        sw->debounce_until_ms = now + CONFIG_LIMIT_SWITCHES_DEBOUNCE_MS;
    }
    return ESP_OK;
}

static void fail_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void start_display(switch_state_t switches[], size_t count)
{
    if (!LIMIT_SWITCHES_DISPLAY_ENABLED) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        publish_switch_ui_state(&switches[i]);
    }
    g_ui_context.switches = switches;
    g_ui_context.count = count;

    bsp_display_cfg_t cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0,
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
    };
    cfg.lv_adapter_cfg.task_stack_size = 16 * 1024;
    cfg.lv_adapter_cfg.task_priority = 10;
    cfg.lv_adapter_cfg.task_core_id = 0;
    cfg.lv_adapter_cfg.tick_period_ms = LVGL_TICK_PERIOD_MS;
    cfg.lv_adapter_cfg.task_min_delay_ms = LVGL_TASK_MIN_DELAY_MS;
    cfg.lv_adapter_cfg.task_max_delay_ms = LVGL_TASK_MAX_DELAY_MS;
    cfg.lv_adapter_cfg.stack_in_psram = false;

    g_disp = bsp_display_start_with_config(&cfg);
    if (g_disp == NULL) {
        ESP_LOGE(TAG, "FAIL LVGL init");
        fail_idle();
    }

    lv_indev_t *touch_indev = bsp_display_get_input_dev();
    if (touch_indev != NULL) {
        esp_err_t ret = esp_lv_adapter_unregister_touch(touch_indev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FAIL unregister unused touch input: %s", esp_err_to_name(ret));
            fail_idle();
        }
    }

    lv_timer_t *ui_refresh_timer;
    bsp_display_lock(-1);
    create_ui(switches, count);
    ui_refresh_timer = lv_timer_create(ui_refresh_timer_cb, DISPLAY_REFRESH_INTERVAL_MS,
                                       &g_ui_context);
    bsp_display_unlock();

    if (ui_refresh_timer == NULL) {
        ESP_LOGE(TAG, "FAIL create UI refresh timer");
        fail_idle();
    }

    ESP_LOGW(TAG,
             "UI mode=dynamic core=0 touch=0 refresh=%d ms tick=%d ms "
             "worker_delay=%d..%d ms stack=internal systimer_level=%d "
             "runtime_bsp_lock=0 full_panel_redraw=0",
             DISPLAY_REFRESH_INTERVAL_MS, LVGL_TICK_PERIOD_MS,
             LVGL_TASK_MIN_DELAY_MS, LVGL_TASK_MAX_DELAY_MS, FREERTOS_SYSTIMER_LEVEL);
}

static esp_err_t configure_switch_input_gpio(gpio_num_t gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&cfg);
}

#if defined(CONFIG_LIMIT_SWITCHES_MODE_INPUT_ONLY)
static void run_input_only_test(void)
{
    const gpio_num_t gpios[] = {LIMIT_SWITCH_1_GPIO, LIMIT_SWITCH_2_GPIO};

    for (size_t i = 0; i < sizeof(gpios) / sizeof(gpios[0]); ++i) {
        esp_err_t ret = configure_switch_input_gpio(gpios[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FAIL configure input-only GPIO%d: %s",
                     gpios[i], esp_err_to_name(ret));
            fail_idle();
        }
    }

    ESP_LOGW(TAG, "INPUT-ONLY TEST: GPIO20/GPIO21 inputs enabled, pullup=1 pulldown=0");
    ESP_LOGW(TAG, "No GPIO reads, interrupts or PCNT are active");

    uint32_t heartbeat = 0;
    while (true) {
        ESP_LOGI(TAG, "INPUT-ONLY heartbeat=%lu uptime_ms=%lu",
                 (unsigned long)++heartbeat, (unsigned long)now_ms());
        vTaskDelay(pdMS_TO_TICKS(STATUS_LOG_INTERVAL_MS));
    }
}
#endif

static void run_gpio_polling_test(switch_state_t switches[], size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        esp_err_t ret = configure_switch_input_gpio(switches[i].gpio);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "FAIL configure polling GPIO%d: %s",
                     switches[i].gpio, esp_err_to_name(ret));
            fail_idle();
        }
        switches[i].stable_level = gpio_get_level(switches[i].gpio);
    }

    start_display(switches, count);

    ESP_LOGW(TAG, "POLLING TEST: GPIO20/GPIO21 inputs enabled, pullup=1 pulldown=0");
    ESP_LOGW(TAG, "Read interval=%d ms; GPIO interrupts and PCNT are disabled; display=%d",
             SWITCH_POLL_INTERVAL_MS, LIMIT_SWITCHES_DISPLAY_ENABLED);
    ESP_LOGI(TAG, "Initial GPIO20=%s GPIO21=%s",
             level_text(switches[0].stable_level), level_text(switches[1].stable_level));

    uint32_t next_status_ms = now_ms() + STATUS_LOG_INTERVAL_MS;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        for (size_t i = 0; i < count; ++i) {
            int level = gpio_get_level(switches[i].gpio);
            if (level == switches[i].stable_level) {
                continue;
            }

            switches[i].raw_edge_count++;
            if (switches[i].stable_level == 1 && level == 0) {
                switches[i].change_count++;
            }
            switches[i].stable_level = level;
            publish_switch_ui_state(&switches[i]);
        }

        uint32_t now = now_ms();
        if ((int32_t)(now - next_status_ms) >= 0) {
            ESP_LOGI(TAG,
                     "POLL STATUS GPIO20=%s falling=%lu transitions=%lu "
                     "GPIO21=%s falling=%lu transitions=%lu",
                     level_text(switches[0].stable_level),
                     (unsigned long)switches[0].change_count,
                     (unsigned long)switches[0].raw_edge_count,
                     level_text(switches[1].stable_level),
                     (unsigned long)switches[1].change_count,
                     (unsigned long)switches[1].raw_edge_count);
            next_status_ms = now + STATUS_LOG_INTERVAL_MS;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SWITCH_POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    switch_state_t switches[] = {
        {.name = "Switch 1", .gpio = LIMIT_SWITCH_1_GPIO},
        {.name = "Switch 2", .gpio = LIMIT_SWITCH_2_GPIO},
    };

    ESP_LOGI(TAG, "BOOT reset=%d display=%d psram_xip=%d",
             (int)esp_reset_reason(), LIMIT_SWITCHES_DISPLAY_ENABLED,
             LIMIT_SWITCHES_PSRAM_XIP_ENABLED);

#if defined(CONFIG_LIMIT_SWITCHES_MODE_INPUT_ONLY)
    run_input_only_test();
    return;
#elif defined(CONFIG_LIMIT_SWITCHES_MODE_POLLING)
    run_gpio_polling_test(switches, sizeof(switches) / sizeof(switches[0]));
    return;
#endif

    ESP_LOGI(TAG, "START GPIO20/GPIO21 PCNT falling-edge test (display/LVGL enabled)");
    ESP_LOGI(TAG, "Input bias: pullup=%d pulldown=%d debounce=%d ms PCNT filter=%d ns",
             SWITCH_PULLUP, SWITCH_PULLDOWN, CONFIG_LIMIT_SWITCHES_DEBOUNCE_MS,
             SWITCH_PCNT_GLITCH_FILTER_NS);

    for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); ++i) {
        if (switch_init(&switches[i]) != ESP_OK) {
            fail_idle();
        }
    }

    start_display(switches, sizeof(switches) / sizeof(switches[0]));

    ESP_LOGI(TAG, "READY falling edges are counted in hardware; CPU polls every %d ms",
             SWITCH_POLL_INTERVAL_MS);

    uint32_t next_status_ms = now_ms() + STATUS_LOG_INTERVAL_MS;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        uint32_t now = now_ms();

        for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); ++i) {
            if (switch_poll(&switches[i], now) != ESP_OK) {
                fail_idle();
            }
            publish_switch_ui_state(&switches[i]);
        }

        if ((int32_t)(now - next_status_ms) >= 0) {
            ESP_LOGI(TAG,
                     "STATUS GPIO20=%s accepted=%lu raw=%lu "
                     "GPIO21=%s accepted=%lu raw=%lu",
                     level_text(switches[0].stable_level), (unsigned long)switches[0].change_count,
                     (unsigned long)switches[0].raw_edge_count,
                     level_text(switches[1].stable_level), (unsigned long)switches[1].change_count,
                     (unsigned long)switches[1].raw_edge_count);
            next_status_ms = now + STATUS_LOG_INTERVAL_MS;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SWITCH_POLL_INTERVAL_MS));
    }
}
