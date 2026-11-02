/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <math.h>
#include <cctype>
#include <cmath>
#include <cstring>
#include <vector>
#include <unistd.h>
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:App:Calculator"
#include "esp_lib_utils.h"
#include "esp_brookesia_app_calculator.hpp"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(img_app_calculator);

#define KEYBOARD_H_PERCENT      70
#define KEYBOARD_FONT           &lv_font_montserrat_20
#define KEYBOARD_SPECIAL_COLOR  lv_color_hex(0xFF3034)
#define KEYBOARD_BG_COLOR       lv_color_hex(0xFFFFFF)

#define LABEL_PAD               3
#define LABEL_FONT_SMALL        &lv_font_montserrat_16
#define LABEL_FONT_BIG          &lv_font_montserrat_20
#define LABEL_COLOR             lv_color_hex(0xFF3034)
#define LABEL_FORMULA_LEN_MAX   256

#define APP_NAME                "Calculator"

// Larger round screens, such as 466x466: keys as circles inside the screen, the display above them
#define ROUND_MIN_SIZE          400
#define ROUND_KEY_SIZE          64
#define ROUND_KEY_GAP_Y         8
#define ROUND_KEYBOARD_Y        36      // Centre of the middle row; every key stays 5 px or more inside the edge
#define ROUND_FORMULA_W         240     // The circle is narrower up at the formula than at the result
#define ROUND_RESULT_W          290
#define ROUND_MIN_SCALE         154     // A long line shrinks to 60 % at most, then shows its end
#define ROUND_FORMULA_Y         -194
#define ROUND_RESULT_Y          -156
#define ROUND_DIGIT_COLOR       lv_color_hex(0x1F232C)
#define ROUND_FUNCTION_COLOR    lv_color_hex(0x3A3F4C)
#define ROUND_OPERATOR_COLOR    lv_color_hex(0xFF9F0A)
#define ROUND_RESULT_COLOR      lv_color_hex(0x9AA0AC)

// Adaptation for 360x360 round screen
#define SCREEN_360_EFFECTIVE_WIDTH  320  // Effective display area for round screen
#define SCREEN_360_EFFECTIVE_HEIGHT 320

static const char *keyboard_map[] = {
    "C", "/", "x", LV_SYMBOL_BACKSPACE, "\n",
    "7", "8", "9", "-", "\n",
    "4", "5", "6", "+", "\n",
    "1", "2", "3", "%", "\n",
    "0", ".", "=", ""
};

// The round keypad is one row of keys per line, each as wide as the circle allows there
struct RoundRow {
    const char *map[6];
    int first_id;   // Index of the row's first key in keyboard_map, as the key handling counts them
    int gap;        // Between the keys of the row
};
static const RoundRow round_rows[] = {
    {{"C", "/", "x", LV_SYMBOL_BACKSPACE, ""}, 0, 14},
    {{"7", "8", "9", "-", ""}, 4, 22},
    {{"4", "5", "6", "+", ""}, 8, 22},
    {{"1", "2", "3", "%", ""}, 12, 14},
    {{"0", ".", "=", ""}, 16, 12},
};

// A double holds about 15 significant digits; longer numbers would come back changed
static constexpr int MAX_NUMBER_DIGITS = 15;
static constexpr int MAX_FORMULA_CHARS = 64;

/* A result as the display shows it: up to 15 significant digits, an exponent past that */
static void format_result(double value, char *out, size_t size)
{
    if (!std::isfinite(value)) {
        snprintf(out, size, "Error");
        return;
    }
    if (value == 0) {
        value = 0;  // No "-0"
    }
    snprintf(out, size, "%.15g", value);
    if (strchr(out, 'e')) {
        snprintf(out, size, "%.9g", value);
    }
}

static constexpr lv_style_selector_t KEYBOARD_ITEMS_DEFAULT =
    static_cast<lv_style_selector_t>(LV_PART_ITEMS) |
    static_cast<lv_style_selector_t>(LV_STATE_DEFAULT);

namespace esp_brookesia::apps {

Calculator *Calculator::_instance = nullptr;

Calculator *Calculator::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Calculator(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Calculator::Calculator(bool use_status_bar, bool use_navigation_bar) :
   App(APP_NAME, &img_app_calculator, true, use_status_bar, use_navigation_bar)
{
}

Calculator::~Calculator()
{
    ESP_UTILS_LOGD("Destroy(@0x%p)", this);
}

bool Calculator::init(void)
{
    ESP_UTILS_LOGD("Init(@0x%p)", this);
    return true;
}

bool Calculator::deinit(void)
{
    ESP_UTILS_LOGD("Deinit(@0x%p)", this);
    return true;
}

bool Calculator::run(void)
{
    ESP_UTILS_LOGD("Run(@0x%p)", this);
    _is_starting = true;

    auto visual_area = getVisualArea();
    _width = lv_area_get_width(&visual_area);
    _height = lv_area_get_height(&visual_area);

    // Special handling for 360x360 round screen
    bool is_round_screen = false;
    int y_offset = 0;  // Vertical offset
    if (_width == 360 && _height == 360) {
        // Round screen, use smaller effective area
        is_round_screen = true;
        _width = SCREEN_360_EFFECTIVE_WIDTH;
        _height = SCREEN_360_EFFECTIVE_HEIGHT;
        y_offset = 20;  // Offset downward by 20 pixels to avoid round top
    } else if (_width == 0 || _height == 0) {
        _width = 400;
        _height = 600;
    }

    formula_len = 1;
    _font_small = LABEL_FONT_SMALL;
    _font_big = LABEL_FONT_BIG;
    _round = false;

    const int32_t hor = lv_display_get_horizontal_resolution(nullptr);
    if ((hor == lv_display_get_vertical_resolution(nullptr)) && (hor >= ROUND_MIN_SIZE)) {
        runRound();
        _is_starting = false;
        return true;
    }

    int keyboard_h = (int)(_height * KEYBOARD_H_PERCENT / 100.0);
    int label_h = _height - keyboard_h;
    int text_h = label_h - 2 * LABEL_PAD;

    lv_obj_set_style_bg_color(lv_scr_act(), KEYBOARD_BG_COLOR, 0);

    keyboard = lv_btnmatrix_create(lv_scr_act());
    lv_btnmatrix_set_map(keyboard, keyboard_map);
    lv_btnmatrix_set_btn_width(keyboard, 18, 1);
    lv_obj_set_size(keyboard, _width, keyboard_h);
    lv_obj_set_style_text_font(keyboard, KEYBOARD_FONT, 0);
    lv_obj_set_style_bg_color(keyboard, KEYBOARD_BG_COLOR, 0);

    // Adjust keyboard position, round screen offset upward to avoid bottom occlusion
    if (is_round_screen) {
        lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -30);  // Offset upward by 30 pixels
    } else {
        lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    }

    lv_obj_add_event_cb(keyboard, keyboard_event_cb, LV_EVENT_ALL, this);
    lv_btnmatrix_set_btn_ctrl(keyboard, 18, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_radius(keyboard, 0, 0);

    // Set special colors for operator buttons (C, /, x, <, -, +, %)
    lv_obj_set_style_text_color(keyboard, KEYBOARD_SPECIAL_COLOR, KEYBOARD_ITEMS_DEFAULT);
    lv_btnmatrix_set_btn_ctrl(keyboard, 0, LV_BTNMATRIX_CTRL_CUSTOM_1);  // C
    lv_btnmatrix_set_btn_ctrl(keyboard, 1, LV_BTNMATRIX_CTRL_CUSTOM_1);  // /
    lv_btnmatrix_set_btn_ctrl(keyboard, 2, LV_BTNMATRIX_CTRL_CUSTOM_1);  // x
    lv_btnmatrix_set_btn_ctrl(keyboard, 3, LV_BTNMATRIX_CTRL_CUSTOM_1);  // <
    lv_btnmatrix_set_btn_ctrl(keyboard, 7, LV_BTNMATRIX_CTRL_CUSTOM_1);  // -
    lv_btnmatrix_set_btn_ctrl(keyboard, 11, LV_BTNMATRIX_CTRL_CUSTOM_1); // +
    lv_btnmatrix_set_btn_ctrl(keyboard, 15, LV_BTNMATRIX_CTRL_CUSTOM_1); // %

    lv_obj_t *label_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(label_obj, _width, label_h);

    // Adjust label area position, round screen offset downward to avoid top occlusion
    if (is_round_screen) {
        lv_obj_align(label_obj, LV_ALIGN_TOP_MID, 0, y_offset);
    } else {
        lv_obj_align(label_obj, LV_ALIGN_TOP_MID, 0, 0);
    }

    lv_obj_set_style_radius(label_obj, 0, 0);
    lv_obj_set_style_border_width(label_obj, 0, 0);
    lv_obj_set_style_pad_all(label_obj, 0, 0);
    lv_obj_set_style_text_font(label_obj, LABEL_FONT_SMALL, 0);
    lv_obj_set_flex_flow(label_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(label_obj, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(label_obj, LABEL_PAD, 0);

    history_label = lv_textarea_create(label_obj);
    lv_obj_set_style_radius(history_label, 0, 0);
    lv_obj_set_style_border_width(history_label, 0, 0);
    lv_obj_set_style_pad_all(history_label, 0, 0);
    lv_obj_set_size(history_label, _width, text_h / 3);
    lv_obj_add_flag(history_label, LV_OBJ_FLAG_SCROLLABLE);

    // Round screen uses center alignment to avoid right edge occlusion
    if (is_round_screen) {
        lv_obj_set_style_text_align(history_label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_set_style_text_align(history_label, LV_TEXT_ALIGN_RIGHT, 0);
    }

    lv_obj_set_style_opa(history_label, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_text_color(history_label, LABEL_COLOR, 0);
    lv_obj_set_style_text_font(history_label, LABEL_FONT_SMALL, 0);
    lv_textarea_set_text(history_label, "");

    lv_obj_t *formula_label_obj = lv_obj_create(label_obj);
    lv_obj_set_size(formula_label_obj, _width, text_h / 3);
    lv_obj_set_style_radius(formula_label_obj, 0, 0);
    lv_obj_set_style_border_width(formula_label_obj, 0, 0);
    lv_obj_set_style_pad_all(formula_label_obj, 0, 0);
    lv_obj_set_style_bg_opa(formula_label_obj, LV_OPA_TRANSP, 0);

    formula_label = lv_label_create(formula_label_obj);
    lv_obj_set_size(formula_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Round screen uses center alignment to avoid right edge occlusion
    if (is_round_screen) {
        lv_obj_align(formula_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(formula_label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_align(formula_label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_align(formula_label, LV_TEXT_ALIGN_RIGHT, 0);
    }

    lv_obj_set_style_text_font(formula_label, LABEL_FONT_BIG, 0);
    lv_label_set_text(formula_label, "0");

    lv_obj_t *result_label_obj = lv_obj_create(label_obj);
    lv_obj_set_size(result_label_obj, _width, text_h / 3);
    lv_obj_set_style_radius(result_label_obj, 0, 0);
    lv_obj_set_style_border_width(result_label_obj, 0, 0);
    lv_obj_set_style_pad_all(result_label_obj, 0, 0);
    lv_obj_set_style_bg_opa(result_label_obj, LV_OPA_TRANSP, 0);

    result_label = lv_label_create(result_label_obj);
    lv_obj_set_size(result_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Round screen uses center alignment to avoid right edge occlusion
    if (is_round_screen) {
        lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        lv_obj_align(result_label, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_RIGHT, 0);
    }

    lv_obj_set_style_text_color(result_label, LABEL_COLOR, 0);
    lv_obj_set_style_text_font(result_label, LABEL_FONT_SMALL, 0);
    lv_label_set_text(result_label, "= 0");

    _is_starting = false;
    return true;
}

void Calculator::runRound(void)
{
    _round = true;
    _font_small = &lv_font_montserrat_24;
    _font_big = &lv_font_montserrat_40;
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    const int rows = sizeof(round_rows) / sizeof(round_rows[0]);
    for (int r = 0; r < rows; r++) {
        int keys = 0;
        while (round_rows[r].map[keys][0] != '\0') {
            keys++;
        }
        lv_obj_t *row = lv_btnmatrix_create(screen);
        lv_btnmatrix_set_map(row, round_rows[r].map);
        lv_obj_set_user_data(row, reinterpret_cast<void *>(static_cast<intptr_t>(round_rows[r].first_id)));
        lv_obj_set_size(row, keys * ROUND_KEY_SIZE + (keys - 1) * round_rows[r].gap, ROUND_KEY_SIZE);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, ROUND_KEYBOARD_Y + (r - rows / 2) * (ROUND_KEY_SIZE + ROUND_KEY_GAP_Y));
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, round_rows[r].gap, 0);
        lv_obj_set_style_radius(row, LV_RADIUS_CIRCLE, LV_PART_ITEMS);
        lv_obj_set_style_border_width(row, 0, LV_PART_ITEMS);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(row, ROUND_DIGIT_COLOR, LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(row, lv_color_white(), LV_PART_ITEMS);
        lv_obj_set_style_text_font(row, &lv_font_montserrat_30, LV_PART_ITEMS);
        // Keys are coloured by kind while they are drawn
        lv_obj_add_flag(row, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
        lv_obj_add_event_cb(row, keyboard_event_cb, LV_EVENT_ALL, this);
        if (r == 0) {
            keyboard = row;
        }
    }

    // The history does not fit on the round screen; it is kept for the shared key handling
    lv_obj_t *history_obj = lv_obj_create(screen);
    lv_obj_add_flag(history_obj, LV_OBJ_FLAG_HIDDEN);
    history_label = lv_textarea_create(history_obj);

    // The display clips a long formula on the left so the newest keys stay in sight
    lv_obj_t *formula_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(formula_obj);
    lv_obj_set_size(formula_obj, ROUND_FORMULA_W, lv_font_get_line_height(_font_big));
    lv_obj_align(formula_obj, LV_ALIGN_CENTER, 0, ROUND_FORMULA_Y);
    lv_obj_remove_flag(formula_obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    formula_label = lv_label_create(formula_obj);
    lv_obj_set_style_text_color(formula_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(formula_label, _font_big, 0);
    lv_label_set_text(formula_label, "0");
    lv_obj_align(formula_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *result_obj = lv_obj_create(screen);
    lv_obj_remove_style_all(result_obj);
    lv_obj_set_size(result_obj, ROUND_RESULT_W, lv_font_get_line_height(_font_big));
    lv_obj_align(result_obj, LV_ALIGN_CENTER, 0, ROUND_RESULT_Y);
    lv_obj_remove_flag(result_obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    result_label = lv_label_create(result_obj);
    lv_obj_set_style_text_color(result_label, ROUND_RESULT_COLOR, 0);
    lv_obj_set_style_text_font(result_label, _font_small, 0);
    lv_label_set_text(result_label, "= 0");
    lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 0);
}

/* A line that is too wide for the display shrinks to fit; past the smallest size it shows its end */
void Calculator::fitRoundLabel(lv_obj_t *label)
{
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t width = lv_obj_get_width(lv_obj_get_parent(label));
    int32_t scale = LV_SCALE_NONE;
    if (size.x > width) {
        scale = LV_MAX(ROUND_MIN_SCALE, width * LV_SCALE_NONE / size.x);
    }
    const bool clip_start = size.x * scale / LV_SCALE_NONE > width;
    lv_obj_set_style_transform_scale(label, scale, 0);
    lv_obj_set_style_transform_pivot_x(label, clip_start ? lv_pct(100) : lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(label, lv_pct(50), 0);
    lv_obj_align(label, clip_start ? LV_ALIGN_RIGHT_MID : LV_ALIGN_CENTER, 0, 0);
}

void Calculator::drawRoundKey(lv_event_t *e)
{
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = static_cast<lv_draw_dsc_base_t *>(lv_draw_task_get_draw_dsc(task));
    if (base->part != LV_PART_ITEMS) {
        return;
    }
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    if (!fill) {
        return;
    }
    lv_obj_t *row = lv_event_get_target_obj(e);
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(row))) + base->id1;
    const bool pressed = lv_btnmatrix_get_selected_btn(row) == base->id1 &&
                         lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_PRESSED);
    lv_color_t color = ROUND_DIGIT_COLOR;
    if (id == 0 || id == 3) {
        color = ROUND_FUNCTION_COLOR;   // C and backspace
    } else if (id == 1 || id == 2 || id == 7 || id == 11 || id == 15 || id == 18) {
        color = ROUND_OPERATOR_COLOR;
    }
    fill->color = pressed ? lv_color_lighten(color, LV_OPA_30) : color;
}

bool Calculator::back(void)
{
    ESP_UTILS_LOGD("Back(@0x%p)", this);

    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");

    return true;
}

bool Calculator::close(void)
{
    ESP_UTILS_LOGD("Close(@0x%p)", this);
    _is_stopping = true;

    _is_stopping = false;
    return true;
}

bool Calculator::isStartZero(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    if ((text_len == 1) && (text[0] == '0')) {
        // LOG_I(TAG, "start 0");
        return true;
    }
    // A "0" just after an operator starts a new number ("5+0"), unlike the one in "0.0"
    if ((text_len >= 2) && (text[text_len - 1] == '0') &&
            !isdigit(static_cast<unsigned char>(text[text_len - 2])) && (text[text_len - 2] != '.')) {
        // LOG_I(TAG, "start 0");
        return true;
    }

    return false;
}

bool Calculator::isStartNum(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    if ((text[text_len - 1] >= '0') && (text[text_len - 1] <= '9')) {
        // LOG_I(TAG, "start num");
        return true;
    }

    return false;
}

bool Calculator::isStartPercent(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    if (text[text_len - 1] == '%') {
        // LOG_I(TAG, "start %");
        return true;
    }

    return false;
}

/* Digits of the number at the end of the formula */
int Calculator::numberDigits(void)
{
    const char *text = lv_label_get_text(formula_label);
    int digits = 0;
    for (int i = strlen(text) - 1; i >= 0 && (isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.'); i--) {
        digits += text[i] != '.';
    }
    return digits;
}

bool Calculator::isLegalDot(void)
{
    const char *text = lv_label_get_text(formula_label);
    int text_len = strlen(text);

    while (text_len-- > 0) {
        if (text[text_len] == '.') {
            // LOG_I(TAG, "illegal dot");
            return false;
        } else if ((text[text_len] < '0') ||
                   (text[text_len] > '9')) {
            return true;
        }
    }

    return true;
}

double Calculator::calculate(const char *input)
{
    std::vector<double> stk;
    int input_len = strlen(input);
    double num = 0;
    bool dot_flag = false;
    int dot_len = 0;
    char pre_sign = '+';

    for (int i = 0; i < input_len; i++) {
        if (input[i] == '.') {
            dot_flag = true;
            dot_len = 0;
        } else if (isdigit(input[i])) {
            if (!dot_flag) {
                num = num * 10 + input[i] - '0';
            } else {
                num += (input[i] - '0') / pow(10.0, ++dot_len);
            }
        } else if (input[i] == '%') {
            num /= 100.0;
        } else if (i != input_len - 1) {
            dot_flag = false;
            dot_len = 0;
            switch (pre_sign) {
            case '+':
                stk.push_back(num);
                break;
            case '-':
                stk.push_back(-num);
                break;
            case 'x':
                stk.back() *= num;
                break;
            default:
                if (num != 0) {
                    stk.back() /= num;
                } else {
                    return NAN;     // Shown as "Error"
                }
            }
            num = 0;
            pre_sign = input[i];
        }

        if (i == input_len - 1) {
            switch (pre_sign) {
            case '+':
                stk.push_back(num);
                break;
            case '-':
                stk.push_back(-num);
                break;
            case 'x':
                stk.back() *= num;
                break;
            default:
                if (num != 0) {
                    stk.back() /= num;
                } else {
                    return NAN;     // Shown as "Error"
                }
            }
            num = 0;
        }
    }

    for (int i = 0; i < stk.size(); i++) {
        num += stk.at(i);
    }
    // LOG_I(TAG, "cal: %s = %f", input, num);

    return num;
}

void Calculator::keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    Calculator *app = (Calculator *)lv_event_get_user_data(e);

    if (code == LV_EVENT_DRAW_TASK_ADDED) {
        if (app->_round) {
            app->drawRoundKey(e);
        }
        return;
    }

    if (code == LV_EVENT_VALUE_CHANGED) {
        // On the round keypad each row is its own matrix and knows the id of its first key
        lv_obj_t *matrix = lv_event_get_target_obj(e);
        const int index = lv_btnmatrix_get_selected_btn(matrix);
        const char *btn_text = lv_btnmatrix_get_btn_text(matrix, index);
        int btn_id = index + static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(matrix)));
        bool calculate_flag = false;
        bool equal_flag = false;
        double res_num;
        char res_str[32];
        char history_str[MAX_FORMULA_CHARS + sizeof(res_str) + 8];

        if (lv_obj_get_style_text_font(app->formula_label, LV_PART_MAIN) == app->_font_small) {
            lv_obj_set_style_text_font(app->formula_label, app->_font_big, 0);
            lv_obj_set_style_text_font(app->result_label, app->_font_small, 0);
        }

        switch (btn_id) {
        // "C"
        case 0:
            lv_label_set_text(app->formula_label, "0");
            app->formula_len = 1;
            calculate_flag = true;
            break;
        // "<"
        case 3:
            if ((app->formula_len == 1) && app->isStartZero()) {
                break;
            }
            lv_label_cut_text(
                app->formula_label,
                --(app->formula_len),
                1
            );
            if (app->formula_len == 0) {
                lv_label_set_text(app->formula_label, "0");
                app->formula_len = 1;
            }
            calculate_flag = true;
            break;
        // "="
        case 18:
            calculate_flag = true;
            equal_flag = true;
            break;
        // "/,X,-,+,%"
        case 1:
        case 2:
        case 7:
        case 11:
        case 15:
            if (app->formula_len >= MAX_FORMULA_CHARS) {
                break;
            }
            // Need number or percent
            if (app->isStartPercent() ||
                    app->isStartNum()) {
                // Firstly input "+, -"
                if (((btn_id == 7) || (btn_id == 11)) &&
                        (app->isStartZero())) {
                    // Remove "0"
                    lv_label_cut_text(
                        app->formula_label,
                        --(app->formula_len),
                        1
                    );
                }
                lv_label_ins_text(
                    app->formula_label,
                    app->formula_len++,
                    btn_text
                );
                if (btn_id == 15) {
                    calculate_flag = true;
                }
            }
            break;
        // "1234567890"
        case 4:
        case 5:
        case 6:
        case 8:
        case 9:
        case 10:
        case 12:
        case 13:
        case 14:
        case 16:
            if (app->formula_len >= MAX_FORMULA_CHARS || app->numberDigits() >= MAX_NUMBER_DIGITS) {
                break;
            }
            // Remove First "0"
            if (app->isStartZero()) {
                lv_label_cut_text(
                    app->formula_label,
                    --(app->formula_len),
                    1
                );
            }
            // Except "%"
            if (!app->isStartPercent()) {
                lv_label_ins_text(
                    app->formula_label,
                    app->formula_len++,
                    btn_text
                );
                calculate_flag = true;
            }
            break;
        // "."
        case 17:
            if (app->formula_len < MAX_FORMULA_CHARS && app->isLegalDot() && app->isStartNum()) {
                lv_label_ins_text(
                    app->formula_label,
                    app->formula_len++,
                    "."
                );
            }
            break;
        default:
            break;
        }

        if (calculate_flag) {
            lv_obj_set_style_text_font(app->formula_label, app->_font_big, 0);

            res_num = app->calculate(lv_label_get_text(app->formula_label));
            format_result(res_num, res_str, sizeof(res_str));
            lv_label_set_text_fmt(app->result_label, "= %s", res_str);
            lv_obj_set_style_text_font(app->result_label, app->_font_small, 0);
        }

        if (equal_flag) {
            lv_obj_set_style_text_font(app->result_label, app->_font_big, 0);

            snprintf(history_str, sizeof(history_str), "\n%.64s = %.31s ", lv_label_get_text(app->formula_label), res_str);
            lv_textarea_set_cursor_pos(app->history_label, strlen(lv_textarea_get_text(app->history_label)));
            lv_textarea_add_text(app->history_label, history_str);

            // A result in exponent form, or an error, cannot be typed on; the next key starts over
            const bool can_continue = !strchr(res_str, 'e') && strcmp(res_str, "Error") != 0;
            lv_label_set_text_fmt(app->formula_label, "%s", can_continue ? res_str : "0");
            lv_obj_set_style_text_font(app->formula_label, app->_font_small, 0);
            app->formula_len = strlen(lv_label_get_text(app->formula_label));
        }

        if (app->_round) {
            // The larger line is the one in focus: the formula while typing, the result after "="
            const bool result_big = lv_obj_get_style_text_font(app->result_label, LV_PART_MAIN) == app->_font_big;
            lv_obj_set_style_text_color(app->formula_label, result_big ? ROUND_RESULT_COLOR : lv_color_white(), 0);
            lv_obj_set_style_text_color(app->result_label, result_big ? lv_color_white() : ROUND_RESULT_COLOR, 0);
            app->fitRoundLabel(app->formula_label);
            app->fitRoundLabel(app->result_label);
        }
    }
}

}
