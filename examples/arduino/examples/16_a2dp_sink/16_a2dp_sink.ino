/*
 * Bluetooth A2DP sink for ESP32-P4-Knob-Touch-AMOLED-1.75.
 */

#include <Arduino.h>

#include <KnobBluetoothPlayer.h>
#include <board_display.h>

namespace {

constexpr char kDeviceName[] = "Knob_BT_INO";
constexpr uint32_t kUiPeriodMs = 100;

KnobBoardDisplay display;
KnobBluetoothPlayer player;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *formatLabel = nullptr;
lv_obj_t *volumeLabel = nullptr;
uint32_t lastUiMs = 0;

lv_obj_t *makeLabel(lv_obj_t *screen, const char *text, int y, uint32_t color)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 360);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

void createUi()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *title = makeLabel(screen, "BLUETOOTH AUDIO", 92, 0xF7F2DF);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    makeLabel(screen, kDeviceName, 135, 0x67D8FF);
    statusLabel = makeLabel(screen, "Starting", 196, 0xF7F2DF);
    formatLabel = makeLabel(screen, "Audio: --", 250, 0x9ED8D0);
    volumeLabel = makeLabel(screen, "Volume: 50%", 304, 0xD7C78F);
    makeLabel(screen, "Turn the knob to adjust volume", 357, 0x9ED8D0);
}

void updateUi()
{
    if (statusLabel == nullptr) {
        return;
    }
    lv_label_set_text_fmt(statusLabel, "%s", player.status());
    if (player.sampleRate() == 0) {
        lv_label_set_text(formatLabel, "Audio: waiting for PCM");
    } else {
        lv_label_set_text_fmt(formatLabel, "Audio: %lu Hz / %s",
                              static_cast<unsigned long>(player.sampleRate()),
                              player.channels() == 1 ? "mono" : "stereo");
    }
    lv_label_set_text_fmt(volumeLabel, "Volume: %u%%", player.volume());
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    if (!display.beginLvgl()) {
        Serial.printf("Display setup failed: %s\n", display.lastError());
        return;
    }
    createUi();
    if (!player.begin(kDeviceName)) {
        updateUi();
        Serial.printf("Bluetooth setup failed: %s\n", player.status());
    }
}

void loop()
{
    player.poll();
    if (millis() - lastUiMs >= kUiPeriodMs) {
        updateUi();
        lastUiMs = millis();
    }
    display.tick();
    delay(5);
}
