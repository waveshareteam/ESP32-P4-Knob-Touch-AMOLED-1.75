/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 *
 * The ES8389 and ES7210 sequences are adapted from Espressif esp_codec_dev
 * device/es8389/es8389.c and device/es7210/es7210.c.
 */

#include "KnobBoardAudio.h"

#include <pin_config.h>

namespace {

struct RegisterValue {
    uint8_t reg;
    uint8_t value;
};

// ES8389 DAC initialization from esp_codec_dev, configured for 16-bit I2S slave mode.
constexpr RegisterValue kCodecStartup[] = {
    {0xF3, 0x00}, {0x00, 0x7E}, {0xF3, 0x38}, {0x24, 0x64}, {0x25, 0x04},
    {0x45, 0x03}, {0x60, 0x2A}, {0x61, 0xC9}, {0x62, 0x4F}, {0x63, 0x06},
    {0x6B, 0x00}, {0x6D, 0x16}, {0x6E, 0xAA}, {0x6F, 0x66}, {0x70, 0x99},
    {0x23, 0x00}, {0x72, 0x10}, {0x73, 0x10}, {0x10, 0xC4}, {0x01, 0x08},
    {0xF1, 0x00}, {0x12, 0x01}, {0x13, 0x01}, {0x14, 0x01}, {0x15, 0x01},
    {0x16, 0x35}, {0x17, 0x09}, {0x18, 0x91}, {0x19, 0x28}, {0x1A, 0x01},
    {0x1B, 0x01}, {0x1C, 0x11}, {0x2A, 0x00}, {0x20, 0x60}, {0x40, 0x60},
    {0xF0, 0x01}, {0x02, 0x00}, {0x04, 0x00}, {0x05, 0x10}, {0x06, 0x00},
    {0x07, 0xC0}, {0x08, 0x00}, {0x09, 0xC0}, {0x0A, 0x80}, {0x0B, 0x04},
    {0x0C, 0x01}, {0x0D, 0x00}, {0x0F, 0x10}, {0x21, 0x1F}, {0x22, 0x7F},
    {0x2F, 0xC0}, {0x30, 0xF4}, {0x31, 0x00}, {0x44, 0x00}, {0x41, 0x7F},
    {0x42, 0x7F}, {0x43, 0x10}, {0x49, 0x0F}, {0x4C, 0xC0}, {0x00, 0x00},
    {0x03, 0xC1}, {0x00, 0x01}, {0x4D, 0x00}, {0x26, 0xBF}, {0x27, 0xBF},
    {0x28, 0xBF}, {0x46, 0xBF}, {0x47, 0xBF}, {0x48, 0xBE},
};

}  // namespace

bool KnobBoardAudio::begin(uint32_t sampleRate, TwoWire &wire)
{
    wire_ = &wire;
    wire_->begin(KNOB_PIN_I2C_SDA, KNOB_PIN_I2C_SCL, 400000);
    wire_->beginTransmission(kCodecAddress);
    if (wire_->endTransmission() != 0) {
        return false;
    }

    pinMode(KNOB_PIN_PA_ENABLE, OUTPUT);
    digitalWrite(KNOB_PIN_PA_ENABLE, LOW);
    i2s_.setPins(KNOB_PIN_I2S_BCLK, KNOB_PIN_I2S_WS, KNOB_PIN_I2S_DOUT,
                 -1, KNOB_PIN_I2S_MCLK);
    if (!i2s_.begin(I2S_MODE_STD, sampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                    I2S_SLOT_MODE_STEREO)) {
        return false;
    }
    if (!configureCodec() || !setVolume(20) || !disablePlayback()) {
        i2s_.end();
        return false;
    }
    sampleRate_ = sampleRate;
    ready_ = true;
    return true;
}

bool KnobBoardAudio::setSampleRate(uint32_t sampleRate)
{
    if (!ready_ || sampleRate == 0 || sampleRate == sampleRate_) {
        return ready_ && sampleRate != 0;
    }
    if (!i2s_.configureTX(sampleRate, I2S_DATA_BIT_WIDTH_16BIT,
                          I2S_SLOT_MODE_STEREO)) {
        return false;
    }
    sampleRate_ = sampleRate;
    return true;
}

bool KnobBoardAudio::enablePlayback()
{
    if (!ready_) {
        return false;
    }
    if (playing_) {
        return true;
    }
    if (!setBiasOn() || !setMute(false)) {
        return false;
    }
    digitalWrite(KNOB_PIN_PA_ENABLE, HIGH);
    playing_ = true;
    return true;
}

bool KnobBoardAudio::disablePlayback()
{
    if (!wire_) {
        return false;
    }
    const bool ok = setMute(true) && stopDac();
    digitalWrite(KNOB_PIN_PA_ENABLE, LOW);
    playing_ = false;
    return ok;
}

size_t KnobBoardAudio::write(const int16_t *samples, size_t sampleCount)
{
    if (!ready_ || !playing_ || !samples) {
        return 0;
    }
    return i2s_.write(samples, sampleCount * sizeof(*samples));
}

bool KnobBoardAudio::setVolume(uint8_t percent)
{
    if (!wire_ || percent > 100) {
        return false;
    }
    // Match esp_codec_dev's default user curve: 0..100 maps to -50..0 dB.
    // The ES8389 driver compensates the 3.3 V DAC / 5 V PA voltage ratio.
    const float db = -50.0f + static_cast<float>(percent) * 0.5f;
    const float hardwareGain = 20.0f * log10f(3.3f / 5.0f);
    const float codecDb = db - hardwareGain;
    const float raw = ((codecDb + 95.5f) * 255.0f) / 127.5f;
    const uint8_t registerValue = static_cast<uint8_t>(constrain(static_cast<int>(raw), 0, 255));
    return writeRegister(0x46, registerValue) && writeRegister(0x47, registerValue);
}

bool KnobBoardAudio::readCodecRegister(uint8_t reg, uint8_t &value)
{
    return wire_ != nullptr && readRegister(reg, value);
}

bool KnobBoardAudio::writeRegister(uint8_t reg, uint8_t value)
{
    wire_->beginTransmission(kCodecAddress);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
}

bool KnobBoardAudio::readRegister(uint8_t reg, uint8_t &value)
{
    wire_->beginTransmission(kCodecAddress);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0 || wire_->requestFrom(kCodecAddress, static_cast<uint8_t>(1)) != 1) {
        return false;
    }
    value = wire_->read();
    return true;
}

bool KnobBoardAudio::updateBits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    return readRegister(reg, current) && writeRegister(reg, (current & ~mask) | (value & mask));
}

bool KnobBoardAudio::configureCodec()
{
    for (const RegisterValue &entry : kCodecStartup) {
        if (!writeRegister(entry.reg, entry.value)) {
            return false;
        }
    }
    // Match this board's codec_dev v2 settings: external MCLK, I2S slave timing,
    // and no ES8389 internal DAC loopback because capture comes from ES7210.
    return updateBits(0x01, 0x01, 0x00) && updateBits(0x02, 0xC3, 0x00) &&
           updateBits(0x23, 0x80, 0x00) && writeRegister(0xF0, 0x12);
}

bool KnobBoardAudio::setMute(bool mute)
{
    return updateBits(0x40, 0x03, mute ? 0x03 : 0x00);
}

bool KnobBoardAudio::setBiasOn()
{
    return writeRegister(0x4D, 0x00) && updateBits(0x69, 0x20, 0x20) &&
           writeRegister(0x61, 0xD9) && writeRegister(0x64, 0x8F) &&
           writeRegister(0x10, 0xE4) && writeRegister(0x00, 0x01) &&
           writeRegister(0x03, 0xC3) && writeRegister(0x24, 0x6A) &&
           writeRegister(0x25, 0x0A);
}

bool KnobBoardAudio::stopDac()
{
    return updateBits(0x61, 0x20, 0x00) && writeRegister(0x10, 0xD4);
}

bool KnobBoardMicrophone::begin(TwoWire &wire)
{
    wire_ = &wire;
    wire_->begin(KNOB_PIN_I2C_SDA, KNOB_PIN_I2C_SCL, 400000);
    wire_->beginTransmission(kCodecAddress);
    if (wire_->endTransmission() != 0) {
        return false;
    }

    // The ES8389 ADC is not connected to the microphones. Keep the adapter PA off.
    pinMode(KNOB_PIN_PA_ENABLE, OUTPUT);
    digitalWrite(KNOB_PIN_PA_ENABLE, LOW);
    i2s_.setPins(KNOB_PIN_I2S_BCLK, KNOB_PIN_I2S_WS, -1,
                 KNOB_PIN_I2S_DIN, KNOB_PIN_I2S_MCLK);
    if (!i2s_.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT,
                    I2S_SLOT_MODE_STEREO)) {
        return false;
    }
    if (!configureCodec() || !startCapture()) {
        i2s_.end();
        return false;
    }
    ready_ = true;
    return true;
}

size_t KnobBoardMicrophone::read(int16_t *samples, size_t sampleCount, uint32_t timeoutMs)
{
    if (!ready_ || !samples || sampleCount == 0 || i2s_.rxChan() == nullptr) {
        return 0;
    }
    size_t bytesRead = 0;
    const esp_err_t result = i2s_channel_read(i2s_.rxChan(), samples,
                                              sampleCount * sizeof(*samples),
                                              &bytesRead, timeoutMs);
    return result == ESP_OK ? bytesRead / sizeof(*samples) : 0;
}

bool KnobBoardMicrophone::writeRegister(uint8_t reg, uint8_t value)
{
    wire_->beginTransmission(kCodecAddress);
    wire_->write(reg);
    wire_->write(value);
    return wire_->endTransmission() == 0;
}

bool KnobBoardMicrophone::readRegister(uint8_t reg, uint8_t &value)
{
    wire_->beginTransmission(kCodecAddress);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0 || wire_->requestFrom(kCodecAddress, static_cast<uint8_t>(1)) != 1) {
        return false;
    }
    value = wire_->read();
    return true;
}

bool KnobBoardMicrophone::updateBits(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t current = 0;
    return readRegister(reg, current) && writeRegister(reg, (current & ~mask) | (value & mask));
}

bool KnobBoardMicrophone::configureCodec()
{
    // ES7210 slave-mode, MIC1/MIC2, 24 dB gain, 16-bit standard I2S.
    constexpr RegisterValue kStartup[] = {
        {0x00, 0xFF}, {0x00, 0x41}, {0x01, 0x3F}, {0x09, 0x30},
        {0x0A, 0x30}, {0x23, 0x2A}, {0x22, 0x0A}, {0x20, 0x0A},
        {0x21, 0x2A}, {0x40, 0x43}, {0x41, 0x70}, {0x42, 0x70},
        {0x07, 0x20}, {0x02, 0xC1}, {0x01, 0x34}, {0x4B, 0x00},
        {0x43, 0x18}, {0x44, 0x18}, {0x12, 0x00}, {0x11, 0x60},
    };
    for (const RegisterValue &entry : kStartup) {
        if (!writeRegister(entry.reg, entry.value)) {
            return false;
        }
    }
    return updateBits(0x08, 0x01, 0x00);
}

bool KnobBoardMicrophone::startCapture()
{
    constexpr RegisterValue kCaptureStart[] = {
        {0x01, 0x34}, {0x06, 0x00}, {0x40, 0x43}, {0x47, 0x08},
        {0x48, 0x08}, {0x49, 0x08}, {0x4A, 0x08}, {0x4B, 0x00},
        {0x43, 0x18}, {0x44, 0x18}, {0x40, 0x43}, {0x00, 0x71},
        {0x00, 0x41},
    };
    for (const RegisterValue &entry : kCaptureStart) {
        if (!writeRegister(entry.reg, entry.value)) {
            return false;
        }
    }
    return true;
}
