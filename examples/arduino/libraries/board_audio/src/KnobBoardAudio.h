/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>

class KnobBoardAudio {
public:
    bool begin(uint32_t sampleRate = 16000, TwoWire &wire = Wire);
    bool enablePlayback();
    bool disablePlayback();
    bool setVolume(uint8_t percent);
    bool readCodecRegister(uint8_t reg, uint8_t &value);
    size_t write(const int16_t *samples, size_t sampleCount);

private:
    static constexpr uint8_t kCodecAddress = 0x10;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t &value);
    bool updateBits(uint8_t reg, uint8_t mask, uint8_t value);
    bool configureCodec();
    bool setMute(bool mute);
    bool setBiasOn();
    bool stopDac();

    TwoWire *wire_ = nullptr;
    I2SClass i2s_{I2S_NUM_1};
    bool ready_ = false;
    bool playing_ = false;
};

class KnobBoardMicrophone {
public:
    bool begin(TwoWire &wire = Wire);
    size_t read(int16_t *samples, size_t sampleCount, uint32_t timeoutMs);

private:
    static constexpr uint8_t kCodecAddress = 0x40;

    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t &value);
    bool updateBits(uint8_t reg, uint8_t mask, uint8_t value);
    bool configureCodec();
    bool startCapture();

    TwoWire *wire_ = nullptr;
    I2SClass i2s_{I2S_NUM_1};
    bool ready_ = false;
};
