# ESP32-P4 Knob Touch AMOLED 1.75 BSP Compatibility Layer

This component keeps the `esp32_p4_platform` component name expected by the
Brookesia example, while using the ESP32-P4 Knob Touch AMOLED 1.75 BSP internals.

## Hardware

- Target: `esp32p4`
- LCD: CO5300 AMOLED, 466x466, MIPI DSI, RGB565, 3 frame buffers
- Touch: CST9217
- LVGL integration: `esp_lvgl_adapter`

## Notes

The `bsp/esp32_p4_platform.h` header is a compatibility wrapper around
`bsp/esp32_p4_knob_touch_amoled_1_75.h`. Some former ESP32-P4 platform BSP
Kconfig symbols are provided as compatibility macros in `bsp/display.h` for
legacy Brookesia apps that still use the old names.