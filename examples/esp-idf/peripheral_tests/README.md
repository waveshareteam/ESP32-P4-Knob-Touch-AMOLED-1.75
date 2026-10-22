# ESP-IDF Peripheral Tests

This directory contains the final 14 independent ESP-IDF peripheral bring-up projects for ESP32-P4-Knob-Touch-AMOLED-1.75. Demos use the board BSP component directly (`test/a2dp_sink/components/esp32_p4_knob_touch_amoled_1_75`, with its companion `esp_lcd_co5300` driver); the top level intentionally contains only the numbered demo applications, this README, and `build_all.ps1`.

Use ESP-IDF v5.5.4, target `esp32p4`, and ccache for validation. Keep build output outside the source tree.

```powershell
$env:IDF_CCACHE_ENABLE = "1"
powershell -ExecutionPolicy Bypass -File examples/esp-idf/peripheral_tests/build_all.ps1 -BuildRoot <external-build-root>
```

Single-project build, flash, and monitor commands use the same project directory and an external build directory:

```powershell
idf.py -C examples/esp-idf/peripheral_tests/03_wifi -B <external-build-root>/03_wifi set-target esp32p4 build
idf.py -C examples/esp-idf/peripheral_tests/03_wifi -B <external-build-root>/03_wifi flash
idf.py -C examples/esp-idf/peripheral_tests/03_wifi -B <external-build-root>/03_wifi monitor
```

## Final Demo Matrix

| Demo | Peripheral coverage | Pins and buses | Expected serial/UI behavior | Status |
| --- | --- | --- | --- | --- |
| `01_system_info` | Chip, flash, PSRAM, internal SRAM totals/free space | On-chip resources | Prints flash, PSRAM, and internal SRAM totals/free values. PSRAM unavailable or init failure is reported as FAILED, not as a success. | Build verified |
| `02_buttons` | BOOT and PWR buttons | BOOT GPIO35, PWR GPIO2 | Independent debounce, edge, and stable-state logs for both inputs. | Build verified |
| `03_wifi` | Wi-Fi scan through external connectivity component | SDIO CLK18/CMD19/D0..D3 14..17, EN54, BOOT6 | `esp_extconn_init()` is the first hardware init. Scan ends with a summary and top AP list; zero APs is explicit. | Build verified; user-reported Wi-Fi tested |
| `04_bluetooth` | BLE GAP aggregate scan through external connectivity component | Same extconn SDIO and control pins as Wi-Fi | Fixed scan window, address/type de-duplication, name-preferred records, and end-of-window Top 10 summary without per-packet address flooding. | Build verified |
| `05_axp2101` | AXP2101 PMU read-only status | I2C0 SDA7/SCL8, address 0x34 | LVGL page shows power, battery, temperature, and register-derived status without writing PMU configuration. | Build verified |
| `06_sd_card` | SDMMC card mount and file I/O | D0..D3 GPIO39..42, CLK43, CMD44; powered by on-chip LDO via BSP | 4-bit mount through BSP `bsp_sdcard_mount()`, mount status, write/read/delete verification, and LVGL state. | Build verified |
| `07_limit_switches` | Two independent limit switches | GPIO20 and GPIO21 | Two LVGL color blocks: low level is green, high level is red. No encoder behavior is claimed. | Build verified |
| `08_motor_drive` | DRV8837-style motor/haptic drive | IN1 GPIO22, IN2 GPIO23; GPIO45 not driven by default | LVGL button drives while pressed, stops on release, and stops on timeout/error/exit. | Build verified |
| `09_rtc` | PCF85063 real-time clock | I2C0 SDA7/SCL8, address 0x51, INT GPIO3 | BCD time is range-checked and displayed as live time; read failures are shown as errors. | Build verified |
| `10_qmi8658` | QMI8658 IMU sampling | I2C0 SDA7/SCL8, address 0x6B or 0x6A, INT1 GPIO4, INT2 GPIO5 | Real accelerometer samples update LVGL charts with bounded buffers and scaled coordinates. | Build verified |
| `11_display_touch` | CO5300 AMOLED and CST9217 touch | LCD RST GPIO29, TE GPIO28; Touch RST GPIO27, INT GPIO26; I2C0 SDA7/SCL8 | Red, green, and blue full-screen stages run first, then a white LVGL drawing page records touch dots and supports clear. LCD and touch resets are independent pins. | Build verified |
| `12_mic_spectrum` | ES7210 microphone spectrum | ES7210 address 0x40, I2S MCLK13/BCLK12/LRCK10/DIN11, ES8389/PA off | 16 kHz capture uses Hann window and esp-dsp FFT. Audio task and LVGL use queue/double-buffer handoff; UI shows spectrum, RMS, peak, state, overrun, and errors. | Build verified |
| `13_speaker_output` | RESERVED speaker output skeleton | PA GPIO53 default low/off; future ES8389 path uses I2C0 and I2S pins | Logs only RESERVED / NOT IMPLEMENTED / UNTESTED style status. It does not initialize ES8389, enable PA, or generate a tone. | Build verified skeleton; RESERVED, not implemented, not tested |
| `14_usb_display` | RESERVED USB display skeleton | Native USB display path pending hardware/software design | Logs only RESERVED / NOT IMPLEMENTED / UNTESTED style status and makes no USB validation claim. | Build verified skeleton; RESERVED, not implemented, not tested |

## Board Support

| Component | Ownership | Notes |
| --- | --- | --- |
| `test/a2dp_sink/components/esp32_p4_knob_touch_amoled_1_75` | Board BSP shared with the main applications | Display/LVGL/touch (`bsp_display_*`), shared I2C bus (`bsp_i2c_*`), SD card (`bsp_sdcard_*`), ES8389 codec init (`bsp_audio_codec_*`). |
| `test/a2dp_sink/components/esp_lcd_co5300` | CO5300 panel driver used by the BSP | Local copy, referenced through `EXTRA_COMPONENT_DIRS`. |
| `espressif/esp-extconn` (managed) | External connectivity for `03_wifi`/`04_bluetooth` | Used directly via `esp_extconn_init()`; no local wrapper. |

Demos that only need on-chip resources or GPIOs (`01_system_info`, `02_buttons`, `13_speaker_output`) use plain ESP-IDF drivers with locally defined pin macros.

## Fixed Managed Components

Component versions are pinned in per-demo manifests and the BSP manifest. `dependencies.lock` is not kept in the source tree because it is generated by the ESP-IDF component manager and would duplicate the manifest pins.

| Component | Version |
| --- | --- |
| ESP-IDF | v5.5.4 |
| `espressif/esp-extconn` | 0.3.3 |
| `espressif/esp_lcd_co5300` | 2.0.3 |
| `waveshare/esp_lcd_touch_cst9217` | 2.0.0 |
| `lvgl/lvgl` | 9.5.0 |
| `espressif/esp_codec_dev` | 1.5.11 |
| `espressif/esp-dsp` | 1.8.2 |

## Hardware Notes

- Display: CO5300 MIPI-DSI AMOLED, 466x466 RGB565, MIPI PHY LDO channel 3 at 2.5 V, TE GPIO28, LCD reset GPIO29.
- Touch: CST9217 on I2C0 SDA GPIO7/SCL GPIO8, 7-bit address 0x5A, INT GPIO26, touch reset GPIO27.
- PMU/RTC/IMU: AXP2101 address 0x34, PCF85063 address 0x51, QMI8658 address 0x6B or 0x6A.
- Buttons and switches: BOOT GPIO35, PWR GPIO2, limit switches GPIO20/GPIO21 with low=green and high=red in the UI.
- Motor/haptic: IN1 GPIO22 and IN2 GPIO23. Implemented tests stop both lines on release, timeout, error, and cleanup.
- SD card: SDMMC D0..D3 GPIO39..42, CLK GPIO43, CMD GPIO44. GPIO45 is not driven by default.
- Audio input: ES7210 microphone capture uses I2S MCLK13, BCLK12, LRCK10, and DIN11. The mic spectrum demo keeps ES8389 output and PA GPIO53 off.
- Speaker output RESERVED: keep PA GPIO53 low before any future codec/I2S work; initialize and validate ES8389 mute/output path before enabling PA; disable PA again on stop or error. Future A2DP input should be added only after the extconn transport, codec output route, volume limits, and PA timing have been confirmed on hardware.
- Wireless: Wi-Fi and BLE demos use `espressif/esp-extconn` 0.3.3 over SDIO CLK18/CMD19/D0..D3 GPIO14..17 with EN GPIO54 and BOOT GPIO6. BLE behavior is GAP scanning only.
- USB display RESERVED: implementation, endpoint role, display protocol, power behavior, and hardware validation are still pending.
