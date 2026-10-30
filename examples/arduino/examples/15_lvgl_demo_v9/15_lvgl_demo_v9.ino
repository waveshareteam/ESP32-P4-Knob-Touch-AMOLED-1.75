#include <Arduino.h>
#include <board_display.h>
#include <lv_demo_benchmark.h>

namespace {

KnobBoardDisplay display;

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("LVGL v9 benchmark");
    if (!display.beginLvgl(320)) {
        Serial.printf("Display setup failed: %s\n", display.lastError());
        return;
    }
    lv_demo_benchmark();
}

void loop()
{
    display.tick();
    delay(5);
}
