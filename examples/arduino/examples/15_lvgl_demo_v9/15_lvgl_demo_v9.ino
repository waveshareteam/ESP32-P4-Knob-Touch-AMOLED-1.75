#include <Arduino.h>
#include <board_display.h>
#include <lv_demo_benchmark.h>

namespace {

KnobBoardDisplay display;

void benchmarkComplete(const lv_demo_benchmark_summary_t *summary)
{
    if (summary == nullptr) {
        Serial.println("Benchmark complete: no valid scenes.");
        return;
    }
    lv_demo_benchmark_summary_display(summary);
    if (summary->valid_scene_cnt == 0) {
        Serial.println("Benchmark complete: no valid scenes.");
        return;
    }
    const uint32_t averageFps = summary->total_avg_fps / summary->valid_scene_cnt;
    Serial.printf("Benchmark complete: %lu valid scenes, %lu average FPS.\n",
                  static_cast<unsigned long>(summary->valid_scene_cnt),
                  static_cast<unsigned long>(averageFps));
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("LVGL v9 benchmark");
    if (!display.beginLvgl()) {
        Serial.printf("Display setup failed: %s\n", display.lastError());
        return;
    }
    lv_demo_benchmark_set_end_cb(benchmarkComplete);
    lv_demo_benchmark();
    lv_obj_t *title = lv_obj_get_child(lv_layer_top(), 0);
    if (title != nullptr) {
        lv_obj_set_width(title, 260);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 32);
    }
}

void loop()
{
    display.tick();
    delay(5);
}
