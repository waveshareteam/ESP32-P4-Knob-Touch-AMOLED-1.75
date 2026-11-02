#include <Arduino.h>

#include <KnobBoardAudio.h>
#include <board_display.h>

#include <math.h>

namespace {

constexpr uint32_t kSampleRateHz = 16000;
constexpr size_t kChannels = 2;
constexpr size_t kFftSize = 512;
constexpr size_t kBarCount = 32;
constexpr uint32_t kReadTimeoutMs = 40;
constexpr uint32_t kUiPeriodMs = 50;
constexpr float kSpectrumGainDb = 24.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kBarsX = 63;
constexpr int kBarsY = 130;
constexpr int kBarsWidth = 340;
constexpr int kBarsHeight = 200;
constexpr int kBarWidth = 7;

KnobBoardDisplay display;
KnobBoardMicrophone microphone;
int16_t pcm[kFftSize * kChannels];
float realPart[kFftSize];
float imagPart[kFftSize];
float hann[kFftSize];
float smoothedBars[kBarCount];
lv_obj_t *statusLabel = nullptr;
lv_obj_t *metricsLabel = nullptr;
lv_obj_t *countsLabel = nullptr;
lv_obj_t *bars[kBarCount] = {};
uint32_t frameCount = 0;
uint32_t readErrors = 0;
uint32_t lastUiUpdateMs = 0;

float clamp(float value, float minimum, float maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

int barX(size_t index)
{
    return kBarsX + static_cast<int>((index * static_cast<size_t>(kBarsWidth - kBarWidth)) / (kBarCount - 1));
}

void createUi()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_clean(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x071013), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "MIC SPECTRUM");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF7F2DF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);

    statusLabel = lv_label_create(screen);
    lv_label_set_text(statusLabel, "Sampling: starting");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x9ED8D0), 0);
    lv_obj_set_width(statusLabel, 270);
    lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *source = lv_label_create(screen);
    lv_label_set_text_fmt(source, "ES7210 MIC1/2 | Display +%d dB", static_cast<int>(kSpectrumGainDb));
    lv_obj_set_style_text_color(source, lv_color_hex(0xD7C78F), 0);
    lv_obj_set_width(source, 270);
    lv_obj_set_style_text_align(source, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(source, LV_ALIGN_TOP_MID, 0, 84);

    for (size_t index = 0; index < kBarCount; ++index) {
        bars[index] = lv_obj_create(screen);
        lv_obj_remove_style_all(bars[index]);
        lv_obj_set_style_bg_opa(bars[index], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bars[index], lv_color_hex(0x25D9A5), 0);
        lv_obj_set_style_radius(bars[index], 2, 0);
        lv_obj_set_pos(bars[index], barX(index), kBarsY + kBarsHeight - 4);
        lv_obj_set_size(bars[index], kBarWidth, 4);
    }

    metricsLabel = lv_label_create(screen);
    lv_label_set_text(metricsLabel, "RMS --.- dBFS  Peak --.- dBFS");
    lv_obj_set_style_text_color(metricsLabel, lv_color_hex(0xF7F2DF), 0);
    lv_obj_set_width(metricsLabel, 280);
    lv_obj_set_style_text_align(metricsLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(metricsLabel, LV_ALIGN_TOP_MID, 0, 365);

    countsLabel = lv_label_create(screen);
    lv_label_set_text(countsLabel, "Frame 0  Read errors 0");
    lv_obj_set_style_text_color(countsLabel, lv_color_hex(0x9ED8D0), 0);
    lv_obj_set_width(countsLabel, 260);
    lv_obj_set_style_text_align(countsLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(countsLabel, LV_ALIGN_TOP_MID, 0, 405);
}

void prepareWindow()
{
    for (size_t index = 0; index < kFftSize; ++index) {
        hann[index] = 0.5f - 0.5f * cosf((2.0f * kPi * static_cast<float>(index)) / static_cast<float>(kFftSize - 1));
    }
}

void fft()
{
    for (size_t index = 1, reversed = 0; index < kFftSize; ++index) {
        size_t bit = kFftSize >> 1;
        while (reversed & bit) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed) {
            const float real = realPart[index];
            realPart[index] = realPart[reversed];
            realPart[reversed] = real;
            const float imaginary = imagPart[index];
            imagPart[index] = imagPart[reversed];
            imagPart[reversed] = imaginary;
        }
    }

    for (size_t length = 2; length <= kFftSize; length <<= 1) {
        const float angle = -2.0f * kPi / static_cast<float>(length);
        const float stepReal = cosf(angle);
        const float stepImaginary = sinf(angle);
        for (size_t start = 0; start < kFftSize; start += length) {
            float twiddleReal = 1.0f;
            float twiddleImaginary = 0.0f;
            for (size_t offset = 0; offset < length / 2; ++offset) {
                const size_t even = start + offset;
                const size_t odd = even + length / 2;
                const float oddReal = realPart[odd] * twiddleReal - imagPart[odd] * twiddleImaginary;
                const float oddImaginary = realPart[odd] * twiddleImaginary + imagPart[odd] * twiddleReal;
                const float evenReal = realPart[even];
                const float evenImaginary = imagPart[even];
                realPart[even] = evenReal + oddReal;
                imagPart[even] = evenImaginary + oddImaginary;
                realPart[odd] = evenReal - oddReal;
                imagPart[odd] = evenImaginary - oddImaginary;
                const float nextReal = twiddleReal * stepReal - twiddleImaginary * stepImaginary;
                twiddleImaginary = twiddleReal * stepImaginary + twiddleImaginary * stepReal;
                twiddleReal = nextReal;
            }
        }
    }
}

void updateUi(float rmsDb, float peakDb)
{
    if (statusLabel == nullptr) {
        return;
    }
    lv_label_set_text_fmt(statusLabel, "Sampling: ES7210 input  Frame %lu", static_cast<unsigned long>(frameCount));
    lv_label_set_text_fmt(metricsLabel, "RMS %.1f dBFS  Peak %.1f dBFS", rmsDb, peakDb);
    lv_label_set_text_fmt(countsLabel, "Frame %lu  Read errors %lu", static_cast<unsigned long>(frameCount),
                          static_cast<unsigned long>(readErrors));

    for (size_t index = 0; index < kBarCount; ++index) {
        const float level = smoothedBars[index];
        const int height = 4 + static_cast<int>(level * static_cast<float>(kBarsHeight - 4));
        const uint32_t color = level > 0.78f ? 0xFF7056 : (level > 0.52f ? 0xFFC857 : 0x25D9A5);
        lv_obj_set_pos(bars[index], barX(index), kBarsY + kBarsHeight - height);
        lv_obj_set_size(bars[index], kBarWidth, height);
        lv_obj_set_style_bg_color(bars[index], lv_color_hex(color), 0);
    }
}

void processFrame()
{
    float sum = 0.0f;
    float sumSquared = 0.0f;
    float peak = 0.0f;
    for (size_t index = 0; index < kFftSize; ++index) {
        const int32_t mixed = static_cast<int32_t>(pcm[index * kChannels]) + pcm[index * kChannels + 1];
        const float sample = static_cast<float>(mixed) / (static_cast<float>(kChannels) * 32768.0f);
        realPart[index] = sample;
        sum += sample;
        sumSquared += sample * sample;
        peak = max(peak, fabsf(sample));
    }

    const float dc = sum / static_cast<float>(kFftSize);
    for (size_t index = 0; index < kFftSize; ++index) {
        realPart[index] = (realPart[index] - dc) * hann[index];
        imagPart[index] = 0.0f;
    }
    fft();

    for (size_t bar = 0; bar < kBarCount; ++bar) {
        size_t start = 1 + (bar * bar * (kFftSize / 2 - 1)) / (kBarCount * kBarCount);
        size_t end = 1 + ((bar + 1) * (bar + 1) * (kFftSize / 2 - 1)) / (kBarCount * kBarCount);
        end = min(end, kFftSize / 2);
        if (end <= start) {
            end = start + 1;
        }
        float magnitudeSum = 0.0f;
        for (size_t bin = start; bin < end; ++bin) {
            magnitudeSum += sqrtf(realPart[bin] * realPart[bin] + imagPart[bin] * imagPart[bin]) / static_cast<float>(kFftSize);
        }
        const float averageMagnitude = magnitudeSum / static_cast<float>(end - start);
        const float db = 20.0f * log10f(averageMagnitude + 1.0e-6f);
        const float normalized = clamp((db + kSpectrumGainDb + 72.0f) / 72.0f, 0.0f, 1.0f);
        smoothedBars[bar] = smoothedBars[bar] * 0.68f + normalized * 0.32f;
    }

    ++frameCount;
    const float rmsDb = 20.0f * log10f(sqrtf(sumSquared / static_cast<float>(kFftSize)) + 1.0e-6f);
    const float peakDb = 20.0f * log10f(peak + 1.0e-6f);
    if (millis() - lastUiUpdateMs >= kUiPeriodMs) {
        updateUi(rmsDb, peakDb);
        lastUiUpdateMs = millis();
    }
}

void showReadError(size_t receivedSamples)
{
    ++readErrors;
    if (statusLabel != nullptr) {
        lv_label_set_text_fmt(statusLabel, "Sampling error: received %u / %u samples",
                              static_cast<unsigned>(receivedSamples), static_cast<unsigned>(kFftSize * kChannels));
        lv_label_set_text_fmt(countsLabel, "Frame %lu  Read errors %lu", static_cast<unsigned long>(frameCount),
                              static_cast<unsigned long>(readErrors));
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("ES7210 microphone spectrum");
    if (!display.beginLvgl()) {
        Serial.printf("Display setup failed: %s\n", display.lastError());
        return;
    }
    createUi();
    prepareWindow();
    if (!microphone.begin()) {
        lv_label_set_text(statusLabel, "ES7210 initialization failed");
        Serial.println("ES7210 initialization failed.");
        return;
    }
    Serial.println("ES7210 sampling at 16 kHz, 512-point FFT; output codec disabled.");
}

void loop()
{
    display.tick();
    const size_t receivedSamples = microphone.read(pcm, kFftSize * kChannels, kReadTimeoutMs);
    if (receivedSamples == kFftSize * kChannels) {
        processFrame();
    } else {
        showReadError(receivedSamples);
    }
    display.tick();
    delay(1);
}
