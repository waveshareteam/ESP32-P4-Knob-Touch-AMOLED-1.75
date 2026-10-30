/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 */

#include "KnobBluetoothPlayer.h"

#include <KnobBoardAudio.h>
#include <KnobBt.h>
#include <pin_config.h>

#include <cstring>

extern "C" {
#include "esp_heap_caps.h"
}

namespace {

constexpr uint32_t kEncoderDebounceUs = 2000;
constexpr uint32_t kPlaybackTimeoutMs = 1500;

KnobBoardAudio audio;

}  // namespace

KnobBluetoothPlayer *KnobBluetoothPlayer::instance_ = nullptr;

bool KnobBluetoothPlayer::begin(const char *deviceName)
{
    if (instance_ != nullptr || deviceName == nullptr || deviceName[0] == '\0') {
        state_ = State::kBackendError;
        return false;
    }

    queue_ = xRingbufferCreateWithCaps(kQueueBytes, RINGBUF_TYPE_NOSPLIT,
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    packet_ = static_cast<uint8_t *>(heap_caps_malloc(kMaxPacketBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    stereoPacket_ = static_cast<uint8_t *>(heap_caps_malloc(kMaxPacketBytes * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (queue_ == nullptr || packet_ == nullptr || stereoPacket_ == nullptr) {
        state_ = State::kNoMemory;
        return false;
    }

    instance_ = this;
    pinMode(KNOB_PIN_KNOB_CW, INPUT_PULLUP);
    pinMode(KNOB_PIN_KNOB_CCW, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(KNOB_PIN_KNOB_CW), encoderClockwise, RISING);
    attachInterrupt(digitalPinToInterrupt(KNOB_PIN_KNOB_CCW), encoderCounterClockwise, RISING);

    if (xTaskCreatePinnedToCore(renderTask, "knob_bt_audio", 8192, this, 4, nullptr, tskNO_AFFINITY) != pdPASS) {
        state_ = State::kNoMemory;
        return false;
    }
    ws_bt_set_pcm_callback(pcmCallback);
    ws_bt_set_event_callback(eventCallback);
    if (ws_bt_begin() != 0 || ws_bt_start_a2dp(deviceName) != 0) {
        state_ = State::kBackendError;
        return false;
    }
    state_ = State::kWaitingForAudio;
    return true;
}

void KnobBluetoothPlayer::poll()
{
    int16_t delta = 0;
    portENTER_CRITICAL(&encoderMux_);
    delta = encoderDelta_;
    encoderDelta_ = 0;
    portEXIT_CRITICAL(&encoderMux_);
    if (delta == 0) {
        return;
    }
    const int next = constrain(static_cast<int>(volume_) + delta * 2, 0, 100);
    volume_ = static_cast<uint8_t>(next);
    if (state_ == State::kPlaying && !audio.setVolume(volume_)) {
        state_ = State::kAudioError;
    }
}

const char *KnobBluetoothPlayer::status() const
{
    if (state_ == State::kPlaying && !isPlaying()) {
        return "Waiting for audio";
    }
    switch (state_) {
        case State::kWaitingForAudio: return "Waiting for audio";
        case State::kPlaying: return "Playing";
        case State::kBackendError: return "Bluetooth start failed";
        case State::kAudioError: return "Audio output failed";
        case State::kNoMemory: return "PSRAM allocation failed";
        default: return "Starting";
    }
}

bool KnobBluetoothPlayer::isPlaying() const
{
    return state_ == State::kPlaying && millis() - lastFrameMs_ < kPlaybackTimeoutMs;
}

uint32_t KnobBluetoothPlayer::sampleRate() const
{
    return sampleRate_;
}

uint8_t KnobBluetoothPlayer::channels() const
{
    return channels_;
}

uint8_t KnobBluetoothPlayer::volume() const
{
    return volume_;
}

uint32_t KnobBluetoothPlayer::droppedPackets() const
{
    return droppedPackets_;
}

void KnobBluetoothPlayer::pcmCallback(const unsigned char *pcm, unsigned int bytes,
                                      unsigned int sampleRate, unsigned int channels)
{
    if (instance_ != nullptr) {
        instance_->enqueuePcm(pcm, bytes, sampleRate, channels);
    }
}

void KnobBluetoothPlayer::eventCallback(int, int)
{
    // The backend's event values are intentionally not interpreted here. PCM is
    // the portable indication that playback is actually active.
}

void IRAM_ATTR KnobBluetoothPlayer::encoderClockwise()
{
    if (instance_ != nullptr) {
        instance_->addEncoderDelta(1);
    }
}

void IRAM_ATTR KnobBluetoothPlayer::encoderCounterClockwise()
{
    if (instance_ != nullptr) {
        instance_->addEncoderDelta(-1);
    }
}

void IRAM_ATTR KnobBluetoothPlayer::addEncoderDelta(int8_t delta)
{
    const uint32_t now = micros();
    portENTER_CRITICAL_ISR(&encoderMux_);
    if (now - lastEncoderUs_ >= kEncoderDebounceUs) {
        encoderDelta_ = constrain(static_cast<int>(encoderDelta_) + delta, -25, 25);
        lastEncoderUs_ = now;
    }
    portEXIT_CRITICAL_ISR(&encoderMux_);
}

void KnobBluetoothPlayer::enqueuePcm(const unsigned char *pcm, unsigned int bytes,
                                     unsigned int sampleRate, unsigned int channels)
{
    if (pcm == nullptr || bytes == 0 || bytes > kMaxPacketBytes || bytes > UINT16_MAX ||
        sampleRate == 0 || (channels != 1 && channels != 2) || (bytes % (channels * sizeof(int16_t))) != 0) {
        ++droppedPackets_;
        return;
    }
    const PacketHeader header = {sampleRate, static_cast<uint16_t>(channels), static_cast<uint16_t>(bytes)};
    const size_t totalBytes = sizeof(header) + bytes;
    void *item = nullptr;
    if (xRingbufferSendAcquire(queue_, &item, totalBytes, 0) != pdTRUE) {
        ++droppedPackets_;
        return;
    }
    uint8_t *destination = static_cast<uint8_t *>(item);
    memcpy(destination, &header, sizeof(header));
    memcpy(destination + sizeof(header), pcm, bytes);
    if (xRingbufferSendComplete(queue_, item) != pdTRUE) {
        ++droppedPackets_;
    }
}

bool KnobBluetoothPlayer::dequeuePacket(PacketHeader &header)
{
    size_t itemBytes = 0;
    void *item = xRingbufferReceive(queue_, &itemBytes, pdMS_TO_TICKS(2));
    if (item == nullptr) {
        return false;
    }
    if (itemBytes < sizeof(header)) {
        vRingbufferReturnItem(queue_, item);
        return false;
    }
    memcpy(&header, item, sizeof(header));
    const bool valid = itemBytes == sizeof(header) + header.bytes &&
                       header.bytes <= kMaxPacketBytes &&
                       (header.channels == 1 || header.channels == 2) &&
                       header.sampleRate != 0;
    if (!valid) {
        vRingbufferReturnItem(queue_, item);
        return false;
    }
    memcpy(packet_, static_cast<uint8_t *>(item) + sizeof(header), header.bytes);
    vRingbufferReturnItem(queue_, item);
    return true;
}

void KnobBluetoothPlayer::renderTask(void *argument)
{
    static_cast<KnobBluetoothPlayer *>(argument)->renderLoop();
}

void KnobBluetoothPlayer::renderLoop()
{
    PacketHeader header = {};
    for (;;) {
        if (!dequeuePacket(header)) {
            continue;
        }
        if (!audioReady_) {
            if (!audio.begin(header.sampleRate) || !audio.setVolume(volume_) || !audio.enablePlayback()) {
                state_ = State::kAudioError;
                continue;
            }
            audioReady_ = true;
            configuredRate_ = header.sampleRate;
        } else if (configuredRate_ != header.sampleRate) {
            if (!audio.setSampleRate(header.sampleRate)) {
                state_ = State::kAudioError;
                continue;
            }
            configuredRate_ = header.sampleRate;
        }

        const int16_t *input = reinterpret_cast<const int16_t *>(packet_);
        const size_t inputFrames = header.bytes / (header.channels * sizeof(int16_t));
        const int16_t *output = input;
        size_t outputSamples = inputFrames * 2;
        if (header.channels == 1) {
            int16_t *stereo = reinterpret_cast<int16_t *>(stereoPacket_);
            for (size_t frame = 0; frame < inputFrames; ++frame) {
                stereo[frame * 2] = input[frame];
                stereo[frame * 2 + 1] = input[frame];
            }
            output = stereo;
        }
        const size_t expectedBytes = outputSamples * sizeof(int16_t);
        if (audio.write(output, outputSamples) != expectedBytes) {
            state_ = State::kAudioError;
            continue;
        }
        sampleRate_ = header.sampleRate;
        channels_ = header.channels;
        lastFrameMs_ = millis();
        state_ = State::kPlaying;
    }
}
