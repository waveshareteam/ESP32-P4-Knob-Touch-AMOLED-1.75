# 03_wifi

Credential-free Wi-Fi scan demo for the ESP32-P4 Knob Touch AMOLED 1.75.

This project keeps the verified wireless contract used by the existing `wifi_scan` baseline:

- `esp_extconn_init()` is the first hardware initialization in `app_main`.
- Wireless transport is provided only by the `espressif/esp-extconn` managed component.
- This project pins `espressif/esp-extconn` to `0.3.3`.
- The ESP32-P4 host does not initialize a local Wi-Fi or Bluetooth controller.
- This demo uses only Wi-Fi scan and reports scan summaries/top APs.

## Output

The serial log prints one scan summary and up to ten access points sorted by RSSI:

```text
summary: status=OK total=8 stored=8 top=8 hidden=0
top01: SSID="example" RSSI=-45 channel=6 auth=WPA2_PSK
```

No per-event or duplicate scan event log is emitted. `0 APs` is a valid environmental result when no networks are visible.

## Build

Build with ESP-IDF v5.5.4 for `esp32p4` and ccache enabled. Use an external short build directory.

```text
idf.py -C examples/esp-idf/peripheral_tests/03_wifi -B <external-build-dir> set-target esp32p4 build
```

