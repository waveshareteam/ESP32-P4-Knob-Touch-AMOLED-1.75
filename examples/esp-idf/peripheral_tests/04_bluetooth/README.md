# 04_bluetooth

BLE GAP scan demo for the ESP32-P4 Knob Touch AMOLED 1.75.

This project intentionally reports human-readable BLE advertisers instead of printing every raw address packet.

Wireless contract:

- `esp_extconn_init()` with `ESP_EXTCONN_CONFIG_DEFAULT()` is the first hardware initialization in `app_main`.
- Wireless transport is provided directly by the `espressif/esp-extconn` managed component.
- `main/idf_component.yml` pins `espressif/esp-extconn` to `0.3.3`.
- This board uses an ESP32-D0WD-V3/D0WDR2-V3 wireless coprocessor on SDIO slot 1 with EN54 active-high, BOOT6, CLK18, CMD19, and D0..D3 on GPIO14..17.
- This page uses only BLE GAP scan through the external coprocessor transport.
- The Wi-Fi host transport is enabled only for the esp-extconn coprocessor boot path; this app does not start Wi-Fi scans, connect, pair, or perform GATT/service discovery.

## Output

Each scan window lasts about 10 seconds. During the window no per-packet address log is printed. At the end the log prints:

- unique advertisers tracked after expiry
- named advertisers
- newly discovered advertisers in this window
- expired advertisers removed from the aggregate table
- strongest advertiser
- up to ten advertisers sorted by RSSI with name, address, address type, RSSI, and seen count

Unnamed advertisers are displayed as `Unnamed`. Complete local name is preferred over short local name.

Future speaker-output or audio transport work belongs in `13_speaker_output`, not in this BLE GAP-only scanner.


## Build

Build with ESP-IDF v5.5.4 for `esp32p4` and ccache enabled. Use an external short build directory.

```text
idf.py -C examples/esp-idf/peripheral_tests/04_bluetooth -B <external-build-dir> set-target esp32p4 build
```

