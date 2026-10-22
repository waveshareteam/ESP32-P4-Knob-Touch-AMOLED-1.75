# 05_axp2101

LVGL read-only AXP2101 PMU status page.

## Behavior

- Uses shared I2C through the BSP (`bsp_i2c_init()` / `bsp_i2c_get_handle()`) at address 0x34.
- Uses a small read-only register reader with AXP2101 registers and conversions cross-checked against XPowersLib source.
- Does not write DCDC/LDO registers, charging registers, ADC enable bits, fuel-gauge registers, or VBUS limit settings.
- Updates the 466x466 LVGL status page once per second.
- Shows chip online state, chip ID, VBUS state/voltage, battery presence/voltage, system voltage, charging state, internal temperature, battery percentage, and explicit `N/A` for unavailable values.

## Pass Conditions

- The screen shows `ONLINE AXP2101` with a valid chip ID read from 0x34.
- VBUS, battery, system voltage, charging state, temperature, and percentage fields update when the corresponding read-only registers return usable data.
- Unsupported, disabled, or unavailable measurements are shown as `N/A`, not as a pass.

## Failure Behavior

I2C probe, chip ID, and data read failures are shown as `ERROR` and logged. On I2C failure the demo removes the device, waits, and retries.

## Hardware Steps

1. Build for target `esp32p4`.
2. Flash and open a serial monitor on real hardware.
3. Connect or disconnect VBUS/battery only as allowed by the board documentation.
4. Confirm the status page changes and no power rail or charging configuration is modified by the demo.

No real-hardware validation was performed by this build-only task.