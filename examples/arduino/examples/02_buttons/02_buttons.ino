#include <Arduino.h>
#include <pin_config.h>

namespace {

constexpr uint32_t kDebounceMs = 30;

struct Button {
    const char *name;
    uint8_t pin;
    uint8_t inputMode;
    int activeLevel;
    int stableLevel;
    int lastLevel;
    uint32_t lastChangeMs;
    uint32_t pressCount;
};

Button buttons[] = {
    {"BOOT", KNOB_PIN_BUTTON_BOOT, INPUT_PULLUP, KNOB_BUTTON_BOOT_ACTIVE_LEVEL, HIGH, HIGH, 0, 0},
    {"PWR", KNOB_PIN_BUTTON_PWR, INPUT, KNOB_BUTTON_PWR_ACTIVE_LEVEL, LOW, LOW, 0, 0},
};

const char *levelName(int level)
{
    return level == LOW ? "LOW" : "HIGH";
}

const char *eventName(const Button &button, int level)
{
    return level == button.activeLevel ? "PRESS" : "RELEASE";
}

void initializeButton(Button &button)
{
    pinMode(button.pin, button.inputMode);
    button.stableLevel = digitalRead(button.pin);
    button.lastLevel = button.stableLevel;
    button.lastChangeMs = millis();

    Serial.printf("%s GPIO%u initial level=%s state=%s count=%lu\n", button.name, button.pin,
                  levelName(button.stableLevel), eventName(button, button.stableLevel), button.pressCount);
}

void updateButton(Button &button, uint32_t now)
{
    const int rawLevel = digitalRead(button.pin);
    if (rawLevel != button.lastLevel) {
        button.lastLevel = rawLevel;
        button.lastChangeMs = now;
    }

    if (rawLevel == button.stableLevel || now - button.lastChangeMs < kDebounceMs) {
        return;
    }

    button.stableLevel = rawLevel;
    if (button.stableLevel == button.activeLevel) {
        ++button.pressCount;
    }
    Serial.printf("%s GPIO%u %s level=%s count=%lu\n", button.name, button.pin,
                  eventName(button, button.stableLevel), levelName(button.stableLevel), button.pressCount);
}

} // namespace

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("Button test");

    for (Button &button : buttons) {
        initializeButton(button);
    }
    Serial.println("Ready: press BOOT and PWR independently.");
}

void loop()
{
    const uint32_t now = millis();
    for (Button &button : buttons) {
        updateButton(button, now);
    }
    delay(10);
}
