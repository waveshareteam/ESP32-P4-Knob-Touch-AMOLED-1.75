# Peripheral Test 13: Speaker Output Reserved

This project is an intentionally safe placeholder for a future speaker-output demo.

## Current behavior

- Target: `esp32p4` with ESP-IDF v5.5.4.
- Configures PA GPIO53 as an output and drives it low at startup.
- Logs `RESERVED`, `NOT IMPLEMENTED`, and `UNTESTED`.
- Does not open ES8389.
- Does not generate a tone.
- Reports no success status.

## Future extension points

A later implementation can add ES8389 speaker output or Classic Bluetooth A2DP audio after real-board validation. That work must define the PA GPIO53 safety sequence, codec mute/volume order, sample format, stream start/stop behavior, and failure-path shutdown before enabling the PA.

Classic audio must not be mixed with BLE GAP scan output from `04_bluetooth`; A2DP should be a separate mode or demo.

## Build

```powershell
idf.py -C examples/esp-idf/peripheral_tests/13_speaker_output -B <short-external-build-dir> set-target esp32p4 build
```

Use ESP-IDF v5.5.4 and enable ccache before building. This build is not a hardware speaker validation.
