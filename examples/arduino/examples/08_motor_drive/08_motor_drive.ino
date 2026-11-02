#include <Arduino.h>
#include <pin_config.h>

namespace {

constexpr uint32_t kMaximumVibrationMs = 400;
constexpr size_t kMaxSerialBytesPerLoop = 8;

bool vibrating = false;
uint32_t vibrationStartedMs = 0;

void stopMotor(const char *reason)
{
    digitalWrite(KNOB_PIN_HAPTIC_IN1, LOW);
    digitalWrite(KNOB_PIN_HAPTIC_IN2, LOW);

    if (vibrating) {
        vibrating = false;
        Serial.printf("Motor stopped: %s\n", reason);
    }
}

void startMotor()
{
    if (vibrating) {
        Serial.println("Motor is already vibrating; original timeout is unchanged.");
        return;
    }

    digitalWrite(KNOB_PIN_HAPTIC_IN2, LOW);
    digitalWrite(KNOB_PIN_HAPTIC_IN1, HIGH);
    vibrationStartedMs = millis();
    vibrating = true;
    Serial.printf("Motor vibrating for up to %lu ms. Send 's' to stop.\n", kMaximumVibrationMs);
}

void processCommand(char command)
{
    switch (command) {
    case 'v':
    case 'V':
        startMotor();
        break;
    case 's':
    case 'S':
        stopMotor("serial command");
        break;
    case '\r':
    case '\n':
        break;
    default:
        Serial.printf("Unknown command '%c'; use 'v' to vibrate or 's' to stop.\n", command);
        break;
    }
}

void stopAtDeadline(uint32_t now)
{
    if (vibrating && now - vibrationStartedMs >= kMaximumVibrationMs) {
        stopMotor("400 ms safety timeout");
    }
}

} // namespace

void setup()
{
    pinMode(KNOB_PIN_HAPTIC_IN1, OUTPUT);
    pinMode(KNOB_PIN_HAPTIC_IN2, OUTPUT);
    stopMotor("startup");

    Serial.begin(115200);
    Serial.println();
    Serial.println("DRV8837 vibration motor test");
    Serial.println("Send 'v' for one 400 ms vibration pulse or 's' to stop it.");
}

void loop()
{
    stopAtDeadline(millis());

    for (size_t bytesProcessed = 0;
         bytesProcessed < kMaxSerialBytesPerLoop && Serial.available() > 0;
         ++bytesProcessed) {
        processCommand(static_cast<char>(Serial.read()));
        stopAtDeadline(millis());
    }
}
