# Peripheral Test 12: Mic Spectrum

Real microphone spectrum demo for ESP32-P4 using the board ES7210 input path.

## Contract

- Target: `esp32p4` with ESP-IDF v5.5.4.
- Input codec: board microphone input, opened through `bsp_audio_codec_microphone_init()`.
- Output codec: disabled. ES8389 is not opened by this demo.
- Speaker PA: forced off before audio/display work starts and kept off on error paths.
- DSP: `espressif/esp-dsp` pinned to `1.8.2`.
- Display: BSP display and LVGL (`bsp_display_start()`) on the 466 x 466 panel.

## Behavior

The capture task reads real I2S PCM from ES7210 at 16 kHz, 16-bit stereo. Samples are mixed to mono, DC is removed, a Hann window is applied, and a 512-point FFT is converted into 32 stable log-spaced bars. The UI task drains a two-slot queue at about 20 fps and displays bars, RMS, peak, queue overrun count, read error count, and status text.

The LVGL path never blocks on I2S. If audio initialization fails, the screen shows the failing stage and the PA remains off.

## Build

```powershell
idf.py -C examples/esp-idf/peripheral_tests/12_mic_spectrum -B <short-external-build-dir> set-target esp32p4 build
```

Use ESP-IDF v5.5.4 and enable ccache before building.

## Real-board acceptance standard

After flashing manually, the screen should show `Sampling: ES7210 input`, `Output codec: disabled`, moving spectrum bars that respond to nearby sound, plausible RMS/peak values, and no growing read error or queue overrun count during normal operation. Any initialization failure must show `FAILED` in the log and, after LVGL is available, on the screen. No sound should be produced by the speaker.
