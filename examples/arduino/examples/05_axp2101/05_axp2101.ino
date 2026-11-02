#include <Arduino.h>
#include <Wire.h>

#include <pin_config.h>

namespace {

constexpr uint8_t kAddress = 0x34;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint32_t kSamplePeriodMs = 1000;

constexpr uint8_t kStatus1 = 0x00;
constexpr uint8_t kStatus2 = 0x01;
constexpr uint8_t kChipId = 0x03;
constexpr uint8_t kAdcChannelControl = 0x30;
constexpr uint8_t kBatteryAdcHigh = 0x34;
constexpr uint8_t kVbusAdcHigh = 0x38;
constexpr uint8_t kSystemAdcHigh = 0x3A;
constexpr uint8_t kTemperatureAdcHigh = 0x3C;
constexpr uint8_t kBatteryPercent = 0xA4;

bool readRegisters(uint8_t reg, uint8_t *data, size_t length, uint8_t &error)
{
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    error = Wire.endTransmission(false);
    if (error != 0) {
        return false;
    }

    const size_t received = Wire.requestFrom(kAddress, static_cast<uint8_t>(length));
    if (received != length) {
        error = 0xFF;
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        data[i] = Wire.read();
    }
    error = 0;
    return true;
}

bool readByte(uint8_t reg, uint8_t &value, uint8_t &error)
{
    return readRegisters(reg, &value, 1, error);
}

bool readH6L8(uint8_t reg, uint16_t &value, uint8_t &error)
{
    uint8_t data[2];
    if (!readRegisters(reg, data, sizeof(data), error)) {
        return false;
    }
    value = (static_cast<uint16_t>(data[0] & 0x3F) << 8) | data[1];
    return true;
}

bool readH5L8(uint8_t reg, uint16_t &value, uint8_t &error)
{
    uint8_t data[2];
    if (!readRegisters(reg, data, sizeof(data), error)) {
        return false;
    }
    value = (static_cast<uint16_t>(data[0] & 0x1F) << 8) | data[1];
    return true;
}

const char *chargeState(uint8_t status2)
{
    switch ((status2 >> 5) & 0x07) {
    case 1: return "charging";
    case 2: return "discharging";
    case 0: return "standby";
    default: return "not charging";
    }
}

const char *chargerStatus(uint8_t status)
{
    switch (status & 0x07) {
    case 0: return "tri";
    case 1: return "pre";
    case 2: return "cc";
    case 3: return "cv";
    case 4: return "done";
    case 5: return "stop";
    default: return "N/A";
    }
}

void printI2cError(uint8_t error)
{
    if (error == 0xFF) {
        Serial.println("AXP2101 read failed: incomplete I2C response; retrying.");
    } else {
        Serial.printf("AXP2101 read failed: I2C code %u; retrying.\n", error);
    }
}

void printSample()
{
    uint8_t error = 0;
    uint8_t chipId = 0;
    if (!readByte(kChipId, chipId, error)) {
        printI2cError(error);
        return;
    }
    if (chipId != 0x47 && chipId != 0x4A) {
        Serial.printf("Unexpected PMU ID 0x%02X at 0x%02X; retrying.\n", chipId, kAddress);
        return;
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t adcChannels = 0;
    if (!readByte(kStatus1, status1, error) || !readByte(kStatus2, status2, error) ||
        !readByte(kAdcChannelControl, adcChannels, error)) {
        printI2cError(error);
        return;
    }

    const bool vbusGood = (status1 & (1U << 5)) != 0;
    const bool batteryPresent = (status1 & (1U << 3)) != 0;
    const bool vbusPresent = ((status2 & (1U << 3)) == 0) && vbusGood;
    const bool temperatureEnabled = (adcChannels & (1U << 4)) != 0;
    const bool systemVoltageEnabled = (adcChannels & (1U << 3)) != 0;
    const bool vbusVoltageEnabled = (adcChannels & (1U << 2)) != 0;
    const bool batteryVoltageEnabled = (adcChannels & (1U << 0)) != 0;
    uint8_t optionalError = 0;

    uint16_t systemMv = 0;
    if (systemVoltageEnabled && !readH6L8(kSystemAdcHigh, systemMv, error)) {
        printI2cError(error);
        return;
    }

    uint16_t temperatureRaw = 0;
    if (temperatureEnabled && !readH6L8(kTemperatureAdcHigh, temperatureRaw, error)) {
        printI2cError(error);
        return;
    }

    Serial.printf("AXP2101 ID=0x%02X ADC=0x%02X VBUS_IN=%s Battery=%s Charge=%s/%s",
                  chipId, adcChannels, vbusPresent ? "present" : "absent",
                  batteryPresent ? "present" : "absent", chargeState(status2), chargerStatus(status2));
    if (systemVoltageEnabled) {
        Serial.printf(" SYS=%u mV", systemMv);
    } else {
        Serial.print(" SYS=N/A(disabled)");
    }
    Serial.print(" Temp=");
    const float temperatureC = 22.0f + (7274.0f - temperatureRaw) / 20.0f;
    if (temperatureEnabled && temperatureRaw <= 16383 && temperatureC > -40.0f && temperatureC < 125.0f) {
        Serial.printf("%.1f C", temperatureC);
    } else if (!temperatureEnabled) {
        Serial.print("N/A(disabled)");
    } else {
        Serial.print("N/A");
    }

    if (vbusPresent && vbusVoltageEnabled) {
        uint16_t vbusMv = 0;
        if (readH6L8(kVbusAdcHigh, vbusMv, error)) {
            Serial.printf(" VBUS=%u mV", vbusMv);
        } else {
            Serial.print(" VBUS=N/A");
            optionalError = error;
        }
    } else if (vbusPresent) {
        Serial.print(" VBUS=N/A(disabled)");
    }
    if (batteryPresent) {
        uint16_t batteryMv = 0;
        uint8_t percent = 0;
        bool batteryVoltageValid = batteryVoltageEnabled && readH5L8(kBatteryAdcHigh, batteryMv, error);
        if (batteryVoltageEnabled && !batteryVoltageValid) {
            optionalError = error;
        }
        if (batteryVoltageValid) {
            Serial.printf(" BAT=%u mV", batteryMv);
        } else if (batteryVoltageEnabled) {
            Serial.print(" BAT=N/A");
        } else {
            Serial.print(" BAT=N/A(disabled)");
        }

        if (readByte(kBatteryPercent, percent, error) && percent <= 100) {
            Serial.printf(" %u%%", percent);
        } else {
            Serial.print(" percent=N/A");
            if (error != 0) {
                optionalError = error;
            }
        }
    }
    Serial.println();
    if (optionalError != 0) {
        printI2cError(optionalError);
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    Wire.begin(KNOB_PIN_I2C_SDA, KNOB_PIN_I2C_SCL, kI2cFrequency);
    Serial.println("AXP2101 read-only status example");
}

void loop()
{
    printSample();
    delay(kSamplePeriodMs);
}
