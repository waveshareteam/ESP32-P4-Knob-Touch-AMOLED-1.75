#include <Arduino.h>
#include <board_display.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace {

KnobBoardDisplay display;
constexpr uint32_t kColorStageMs = 2000;
constexpr int16_t kBrushRadius = 3;
constexpr size_t kCanvasBytes = KnobBoardDisplay::kWidth * KnobBoardDisplay::kHeight * 2;

uint8_t colorStage = 0;
uint32_t stageStartedMs = 0;
uint32_t strokeCount = 0;
lv_obj_t *canvas = nullptr;
lv_obj_t *strokeCountLabel = nullptr;
uint8_t *canvasBuffer = nullptr;
bool penDown = false;
lv_point_t previousPoint{};

bool drawablePoint(const lv_point_t &point)
{
    return point.x >= 0 && point.x < KnobBoardDisplay::kWidth &&
           point.y >= 0 && point.y < KnobBoardDisplay::kHeight;
}

void paintBrush(const lv_point_t &center)
{
    const lv_color_t ink = lv_color_hex(0x111827);
    for (int16_t y = center.y - kBrushRadius; y <= center.y + kBrushRadius; ++y) {
        for (int16_t x = center.x - kBrushRadius; x <= center.x + kBrushRadius; ++x) {
            const int16_t dx = x - center.x;
            const int16_t dy = y - center.y;
            const lv_point_t point = {x, y};
            if (dx * dx + dy * dy <= kBrushRadius * kBrushRadius && drawablePoint(point)) {
                lv_canvas_set_px(canvas, x, y, ink, LV_OPA_COVER);
            }
        }
    }
}

void drawSegment(const lv_point_t &start, const lv_point_t &end)
{
    lv_display_t *lvDisplay = lv_obj_get_display(canvas);
    lv_display_enable_invalidation(lvDisplay, false);

    int16_t x = start.x;
    int16_t y = start.y;
    const int16_t dx = abs(end.x - start.x);
    const int16_t sx = start.x < end.x ? 1 : -1;
    const int16_t dy = -abs(end.y - start.y);
    const int16_t sy = start.y < end.y ? 1 : -1;
    int16_t error = dx + dy;
    while (true) {
        paintBrush({x, y});
        if (x == end.x && y == end.y) {
            break;
        }
        const int16_t twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x += sx;
        }
        if (twiceError <= dx) {
            error += dx;
            y += sy;
        }
    }

    lv_display_enable_invalidation(lvDisplay, true);
    lv_obj_invalidate(canvas);
}

void stopStroke()
{
    penDown = false;
}

void drawingEvent(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        stopStroke();
        return;
    }

    lv_indev_t *input = lv_indev_get_act();
    if (input == nullptr || (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING)) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (!drawablePoint(point)) {
        stopStroke();
        return;
    }

    if (code == LV_EVENT_PRESSED || !penDown) {
        penDown = true;
        previousPoint = point;
        ++strokeCount;
        lv_label_set_text_fmt(strokeCountLabel, "Strokes: %lu", static_cast<unsigned long>(strokeCount));
        drawSegment(point, point);
        return;
    }

    drawSegment(previousPoint, point);
    previousPoint = point;
}

void clearCanvas(lv_event_t *)
{
    std::memset(canvasBuffer, 0xFF, kCanvasBytes);
    strokeCount = 0;
    penDown = false;
    lv_label_set_text(strokeCountLabel, "Strokes: 0");
    lv_obj_invalidate(canvas);
}

bool showTouchPage()
{
    canvasBuffer = static_cast<uint8_t *>(heap_caps_malloc(kCanvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (canvasBuffer == nullptr) {
        Serial.println("Touch canvas allocation failed.");
        return false;
    }
    std::memset(canvasBuffer, 0xFF, kCanvasBytes);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_remove_event_cb(screen, drawingEvent);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, drawingEvent, LV_EVENT_ALL, nullptr);

    canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas, canvasBuffer, KnobBoardDisplay::kWidth, KnobBoardDisplay::kHeight, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Touch drawing");
    lv_obj_set_style_text_color(title, lv_color_hex(0x111827), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 25);

    strokeCountLabel = lv_label_create(screen);
    lv_label_set_text(strokeCountLabel, "Strokes: 0");
    lv_obj_set_pos(strokeCountLabel, 105, 65);
    lv_obj_set_style_text_color(strokeCountLabel, lv_color_hex(0x111827), 0);

    lv_obj_t *button = lv_button_create(screen);
    lv_obj_set_size(button, 90, 44);
    lv_obj_set_pos(button, 280, 55);
    lv_obj_add_event_cb(button, clearCanvas, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, "Clear");
    lv_obj_center(label);
    return true;
}

void showColorStage(uint32_t color)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(color), 0);
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("CO5300 MIPI-DSI display and CST9217 touch test");
    if (!display.beginLvgl()) {
        Serial.printf("Display setup failed: %s\n", display.lastError());
        return;
    }
    showColorStage(0xFF0000);
    stageStartedMs = millis();
}

void loop()
{
    display.tick();
    if (colorStage < 3 && millis() - stageStartedMs >= kColorStageMs) {
        ++colorStage;
        stageStartedMs = millis();
        if (colorStage == 1) {
            showColorStage(0x00FF00);
        } else if (colorStage == 2) {
            showColorStage(0x0000FF);
        } else if (showTouchPage()) {
            Serial.println("Touch drawing page ready.");
        }
    }
    delay(5);
}
