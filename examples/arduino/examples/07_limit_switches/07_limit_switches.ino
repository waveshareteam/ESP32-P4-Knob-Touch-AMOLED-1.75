#include <Arduino.h>
#include <pin_config.h>

namespace {

constexpr uint32_t kDebounceMs = 2;
constexpr uint32_t kPollIntervalMs = 1;

struct DirectionSwitch {
    const char *name;
    uint8_t pin;
    int stableLevel;
    int rawLevel;
    uint32_t rawLevelSinceMs;
    uint32_t triggerCount;
};

DirectionSwitch switches[] = {
    {"CW", KNOB_PIN_KNOB_CW, HIGH, HIGH, 0, 0},
    {"CCW", KNOB_PIN_KNOB_CCW, HIGH, HIGH, 0, 0},
};

const char *levelName(int level)
{
    return level == LOW ? "LOW" : "HIGH";
}

void initializeSwitch(DirectionSwitch &directionSwitch)
{
    pinMode(directionSwitch.pin, INPUT_PULLUP);
    directionSwitch.stableLevel = digitalRead(directionSwitch.pin);
    directionSwitch.rawLevel = directionSwitch.stableLevel;
    directionSwitch.rawLevelSinceMs = millis();

    Serial.printf("%s GPIO%u initial level=%s triggers=%lu\n", directionSwitch.name,
                  directionSwitch.pin, levelName(directionSwitch.stableLevel),
                  directionSwitch.triggerCount);
}

void updateSwitch(DirectionSwitch &directionSwitch, uint32_t now)
{
    const int rawLevel = digitalRead(directionSwitch.pin);
    if (rawLevel != directionSwitch.rawLevel) {
        directionSwitch.rawLevel = rawLevel;
        directionSwitch.rawLevelSinceMs = now;
    }

    if (directionSwitch.rawLevel == directionSwitch.stableLevel ||
        now - directionSwitch.rawLevelSinceMs < kDebounceMs) {
        return;
    }

    const int previousLevel = directionSwitch.stableLevel;
    directionSwitch.stableLevel = directionSwitch.rawLevel;
    if (previousLevel == HIGH && directionSwitch.stableLevel == LOW) {
        ++directionSwitch.triggerCount;
        Serial.printf("%s trigger GPIO%u count=%lu\n", directionSwitch.name,
                      directionSwitch.pin, directionSwitch.triggerCount);
    } else {
        Serial.printf("%s release GPIO%u\n", directionSwitch.name, directionSwitch.pin);
    }
}

} // namespace

void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("Knob direction switch test");
    Serial.println("CW and CCW are independent active-low switches, not a quadrature encoder.");

    for (DirectionSwitch &directionSwitch : switches) {
        initializeSwitch(directionSwitch);
    }
}

void loop()
{
    const uint32_t now = millis();
    for (DirectionSwitch &directionSwitch : switches) {
        updateSwitch(directionSwitch, now);
    }
    delay(kPollIntervalMs);
}
