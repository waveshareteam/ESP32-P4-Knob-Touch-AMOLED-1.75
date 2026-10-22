# 01_system_info

Read-only ESP32-P4 system capacity check for flash, PSRAM configuration, and heap/SRAM.

## Behavior

- Does not initialize the display or any external peripheral.
- Prints flash chip capacity using `esp_flash_get_size()`.
- Prints PSRAM configuration state and, when PSRAM is enabled and initialized, capacity from `esp_psram_get_size()` plus SPIRAM heap totals.
- Prints internal heap/SRAM total, current free heap, and minimum-ever free heap with `heap_caps_get_total_size()`, `heap_caps_get_free_size()`, and `heap_caps_get_minimum_free_size()`.
- Any API failure, or any required capacity returning zero, is logged as `FAIL`. A zero PSRAM capacity is not reported as a pass condition.

## Pass Conditions

- Flash size API succeeds and returns a non-zero capacity.
- Internal heap/SRAM total, current free, and minimum free are all non-zero.
- If PSRAM is enabled in configuration, PSRAM must be initialized and report a non-zero capacity.

## Failure Behavior

The demo logs `FAIL` with the API name and error code or the zero value that caused the failure, then remains idle for serial inspection.

## Hardware Steps

1. Build for target `esp32p4`.
2. Flash and open a serial monitor on real hardware.
3. Confirm the serial log contains capacity values and a final `PASS` only when all required checks succeed.

No real-hardware validation was performed by this build-only task.
