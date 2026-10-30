#include <Arduino.h>
#include <Wire.h>

#include <pin_config.h>

namespace {

constexpr uint8_t kAddress = 0x51;
constexpr uint8_t kSecondsRegister = 0x04;
constexpr uint32_t kI2cFrequency = 100000;
constexpr uint32_t kSamplePeriodMs = 1000;

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

uint8_t fromBcd(uint8_t value)
{
    return ((value >> 4) * 10U) + (value & 0x0F);
}

bool decodeBcd(uint8_t raw, uint8_t mask, uint8_t minimum, uint8_t maximum, uint8_t &value)
{
    const uint8_t bcd = raw & mask;
    if ((bcd & 0x0F) > 9 || ((bcd >> 4) & 0x0F) > 9) {
        return false;
    }
    value = fromBcd(bcd);
    return value >= minimum && value <= maximum;
}

bool isLeapYear(uint8_t year)
{
    const uint16_t fullYear = 2000U + year;
    return (fullYear % 4U == 0U && fullYear % 100U != 0U) || fullYear % 400U == 0U;
}

uint8_t daysInMonth(uint8_t month, uint8_t year)
{
    static constexpr uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    return month == 2 && isLeapYear(year) ? 29 : kDays[month - 1];
}

void printI2cError(uint8_t error)
{
    if (error == 0xFF) {
        Serial.println("PCF85063 read failed: incomplete I2C response; retrying.");
    } else {
        Serial.printf("PCF85063 read failed: I2C code %u; retrying.\n", error);
    }
}

void printSample()
{
    uint8_t registers[7];
    uint8_t error = 0;
    if (!readRegisters(kSecondsRegister, registers, sizeof(registers), error)) {
        printI2cError(error);
        return;
    }

    uint8_t second = 0;
    uint8_t minute = 0;
    uint8_t hour = 0;
    uint8_t day = 0;
    uint8_t weekday = 0;
    uint8_t month = 0;
    uint8_t year = 0;
    const bool valid = decodeBcd(registers[0], 0x7F, 0, 59, second) &&
                       decodeBcd(registers[1], 0x7F, 0, 59, minute) &&
                       decodeBcd(registers[2], 0x3F, 0, 23, hour) &&
                       decodeBcd(registers[3], 0x3F, 1, 31, day) &&
                       decodeBcd(registers[4], 0x07, 0, 6, weekday) &&
                       decodeBcd(registers[5], 0x1F, 1, 12, month) &&
                       decodeBcd(registers[6], 0xFF, 0, 99, year) &&
                       day <= daysInMonth(month, year);
    if (!valid) {
        Serial.printf("PCF85063 invalid BCD/date data; oscillator_stop=%s; retrying.\n",
                      (registers[0] & 0x80) ? "yes" : "no");
        return;
    }

    Serial.printf("PCF85063 20%02u-%02u-%02u %02u:%02u:%02u weekday=%u oscillator_stop=%s INT=%d\n",
                  year, month, day, hour, minute, second, weekday,
                  (registers[0] & 0x80) ? "yes" : "no", digitalRead(KNOB_PIN_RTC_INT));
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    pinMode(KNOB_PIN_RTC_INT, INPUT_PULLUP);
    Wire.begin(KNOB_PIN_I2C_SDA, KNOB_PIN_I2C_SCL, kI2cFrequency);
    Serial.println("PCF85063 read-only clock example");
}

void loop()
{
    printSample();
    delay(kSamplePeriodMs);
}
