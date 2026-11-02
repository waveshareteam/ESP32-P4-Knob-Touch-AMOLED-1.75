#include <Arduino.h>

#include <KnobBoardAudio.h>

namespace {

constexpr uint32_t kSampleRateHz = 48000;
constexpr uint32_t kToneHz = 1000;
constexpr uint32_t kToneDurationMs = 3000;
constexpr size_t kChannels = 2;
constexpr size_t kChunkFrames = 480;
constexpr int16_t kToneAmplitude = 6553;
constexpr uint8_t kVolume = 70;

KnobBoardAudio speaker;
bool playTone = true;

int16_t toneSample(uint32_t phase)
{
    const uint32_t position = phase >> 16;
    const int32_t triangle = position < 32768U ? static_cast<int32_t>(position * 2U)
                                                : static_cast<int32_t>((65535U - position) * 2U);
    return static_cast<int16_t>(((triangle - 32768) * kToneAmplitude) / 32768);
}

bool writeAudio(bool tone)
{
    int16_t samples[kChunkFrames * kChannels];
    const size_t frames = kSampleRateHz * kToneDurationMs / 1000;
    const uint32_t phaseStep = static_cast<uint32_t>((static_cast<uint64_t>(kToneHz) << 32) / kSampleRateHz);
    uint32_t phase = 0;
    bool wroteAllSamples = true;

    for (size_t offset = 0; offset < frames; offset += kChunkFrames) {
        const size_t chunkFrames = min(kChunkFrames, frames - offset);
        for (size_t frame = 0; frame < chunkFrames; ++frame) {
            const int16_t sample = tone ? toneSample(phase) : 0;
            samples[frame * kChannels] = sample;
            samples[frame * kChannels + 1] = sample;
            phase += phaseStep;
        }
        const size_t expectedBytes = chunkFrames * kChannels * sizeof(int16_t);
        if (speaker.write(samples, chunkFrames * kChannels) != expectedBytes) {
            wroteAllSamples = false;
            break;
        }
    }

    return wroteAllSamples;
}

void printCodecState()
{
    constexpr uint8_t kRegisters[] = {0x02, 0x03, 0x10, 0x40, 0x46, 0x47, 0x61, 0x64, 0xF0};
    for (uint8_t reg : kRegisters) {
        uint8_t value = 0;
        if (speaker.readCodecRegister(reg, value)) {
            Serial.printf("ES8389[0x%02X]=0x%02X\n", reg, value);
        } else {
            Serial.printf("ES8389 register read failed: 0x%02X\n", reg);
        }
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("ES8389 speaker output diagnostic");
    if (!speaker.begin(kSampleRateHz)) {
        Serial.println("ES8389 initialization failed.");
        return;
    }
    if (!speaker.setVolume(kVolume) || !speaker.enablePlayback()) {
        Serial.println("ES8389 playback enable failed.");
        return;
    }
    printCodecState();
    Serial.println("Playing 1 kHz for 3 s, then digital silence for 3 s.");
}

void loop()
{
    if (!writeAudio(playTone)) {
        Serial.println("I2S write failed.");
    }
    playTone = !playTone;
    Serial.println(playTone ? "Tone interval." : "Digital silence interval.");
}
