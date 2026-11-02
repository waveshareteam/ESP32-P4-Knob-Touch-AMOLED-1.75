/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <Arduino.h>
#include <freertos/ringbuf.h>

class KnobBluetoothPlayer {
public:
    bool begin(const char *deviceName);
    void poll();

    const char *status() const;
    bool isPlaying() const;
    uint32_t sampleRate() const;
    uint8_t channels() const;
    uint8_t volume() const;
    uint32_t droppedPackets() const;

private:
    static constexpr size_t kQueueBytes = 96 * 1024;
    static constexpr size_t kMaxPacketBytes = 8192;

    enum class State : uint8_t {
        kNotStarted,
        kWaitingForAudio,
        kPlaying,
        kBackendError,
        kAudioError,
        kNoMemory,
    };

    struct PacketHeader {
        uint32_t sampleRate;
        uint16_t channels;
        uint16_t bytes;
    };

    static void pcmCallback(const unsigned char *pcm, unsigned int bytes,
                            unsigned int sampleRate, unsigned int channels);
    static void eventCallback(int event, int value);
    static void IRAM_ATTR encoderClockwise();
    static void IRAM_ATTR encoderCounterClockwise();
    static void renderTask(void *argument);

    void enqueuePcm(const unsigned char *pcm, unsigned int bytes,
                    unsigned int sampleRate, unsigned int channels);
    bool dequeuePacket(PacketHeader &header);
    void renderLoop();
    void IRAM_ATTR addEncoderDelta(int8_t delta);

    RingbufHandle_t queue_ = nullptr;
    uint8_t *packet_ = nullptr;
    uint8_t *stereoPacket_ = nullptr;
    uint32_t configuredRate_ = 0;
    volatile uint32_t lastFrameMs_ = 0;
    volatile uint32_t droppedPackets_ = 0;
    volatile uint32_t sampleRate_ = 0;
    volatile uint8_t channels_ = 0;
    volatile uint8_t volume_ = 50;
    volatile int16_t encoderDelta_ = 0;
    volatile uint32_t lastEncoderUs_ = 0;
    volatile State state_ = State::kNotStarted;
    bool audioReady_ = false;
    portMUX_TYPE encoderMux_ = portMUX_INITIALIZER_UNLOCKED;

    static KnobBluetoothPlayer *instance_;
};
