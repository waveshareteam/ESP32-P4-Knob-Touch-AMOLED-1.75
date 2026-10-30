#include <Arduino.h>
#include <Wire.h>

#include <pin_config.h>

namespace {

constexpr uint8_t kPrimaryAddress = 0x6B;
constexpr uint8_t kAlternateAddress = 0x6A;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint32_t kSamplePeriodMs = 25;
constexpr uint32_t kPrintPeriodMs = 100;

constexpr uint8_t kWhoAmI = 0x00;
constexpr uint8_t kCtrl1 = 0x02;
constexpr uint8_t kCtrl2 = 0x03;
constexpr uint8_t kCtrl7 = 0x08;
constexpr uint8_t kAccelerationXLow = 0x35;
constexpr uint8_t kExpectedWhoAmI = 0x05;
constexpr uint8_t kAutoIncrementLittleEndian = 0x40;
constexpr uint8_t kAcceleration250HzRange8G = 0x23;
constexpr uint8_t kEnableAcceleration = 0x01;
constexpr float kAccelerationLsbPerG = 4096.0f;

uint8_t g_address = 0;
uint8_t g_whoAmI = 0;
uint32_t g_lastPrintMs = 0;

bool readRegisters(uint8_t address, uint8_t reg, uint8_t *data, size_t length, uint8_t &error)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    error = Wire.endTransmission(false);
    if (error != 0) {
        return false;
    }

    const size_t received = Wire.requestFrom(address, static_cast<uint8_t>(length));
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

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value, uint8_t &error)
{
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    error = Wire.endTransmission();
    return error == 0;
}

int16_t littleEndianInt16(const uint8_t *data)
{
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

void printI2cError(const char *operation, uint8_t error)
{
    if (error == 0xFF) {
        Serial.printf("QMI8658 %s failed: incomplete I2C response; retrying.\n", operation);
    } else {
        Serial.printf("QMI8658 %s failed: I2C code %u; retrying.\n", operation, error);
    }
}

bool initializeSensor()
{
    uint8_t error = 0;
    for (const uint8_t address : {kPrimaryAddress, kAlternateAddress}) {
        uint8_t whoAmI = 0;
        if (!readRegisters(address, kWhoAmI, &whoAmI, 1, error)) {
            continue;
        }
        if (whoAmI != kExpectedWhoAmI) {
            Serial.printf("Unexpected QMI8658 ID 0x%02X at 0x%02X; retrying.\n", whoAmI, address);
            continue;
        }
        if (!writeRegister(address, kCtrl1, kAutoIncrementLittleEndian, error) ||
            !writeRegister(address, kCtrl2, kAcceleration250HzRange8G, error) ||
            !writeRegister(address, kCtrl7, kEnableAcceleration, error)) {
            printI2cError("configuration", error);
            return false;
        }
        g_address = address;
        g_whoAmI = whoAmI;
        Serial.printf("QMI8658 online at 0x%02X, WHO_AM_I=0x%02X, accel=+/-8g at 250 Hz.\n", g_address, g_whoAmI);
        return true;
    }
    printI2cError("probe", error);
    return false;
}

void sampleAcceleration()
{
    uint8_t raw[6];
    uint8_t error = 0;
    if (!readRegisters(g_address, kAccelerationXLow, raw, sizeof(raw), error)) {
        printI2cError("acceleration read", error);
        g_address = 0;
        return;
    }

    if (millis() - g_lastPrintMs < kPrintPeriodMs) {
        return;
    }
    g_lastPrintMs = millis();
    const float ax = littleEndianInt16(&raw[0]) / kAccelerationLsbPerG;
    const float ay = littleEndianInt16(&raw[2]) / kAccelerationLsbPerG;
    const float az = littleEndianInt16(&raw[4]) / kAccelerationLsbPerG;
    Serial.printf("QMI8658 X=%+.3f g Y=%+.3f g Z=%+.3f g INT1=%d INT2=%d\n",
                  ax, ay, az, digitalRead(KNOB_PIN_IMU_INT1), digitalRead(KNOB_PIN_IMU_INT2));
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    pinMode(KNOB_PIN_IMU_INT1, INPUT);
    pinMode(KNOB_PIN_IMU_INT2, INPUT);
    Wire.begin(KNOB_PIN_I2C_SDA, KNOB_PIN_I2C_SCL, kI2cFrequency);
    Serial.println("QMI8658 accelerometer example");
}

void loop()
{
    if (g_address == 0) {
        initializeSensor();
        delay(g_address == 0 ? 500 : kSamplePeriodMs);
        return;
    }
    sampleAcceleration();
    delay(kSamplePeriodMs);
}
