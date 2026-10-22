# 09_rtc

LVGL PCF85063 RTC read-only clock test.

## Behavior

- Uses the BSP shared I2C bus through `bsp_i2c_init()`/`bsp_i2c_get_handle()` and removes the RTC device on error cleanup paths.
- Does not write the RTC time by default and does not include a sample-time write mode.
- Reads PCF85063 time registers once per second in a worker task, not in the LVGL timer loop.
- Validates BCD nibbles, month values, and month-specific day ranges before displaying a time.
- Shows date, weekday, INT GPIO3 level, oscillator-stop/invalid status, and I2C errors.
- A read failure clears the clock display to `--:--:--`; the UI does not keep showing stale or fabricated time.

## Pass Conditions

- The status line shows the PCF85063 online at address 0x51.
- The clock updates once per second with valid BCD-decoded fields.
- The oscillator status is visible; if the oscillator-stop flag is set, the clock is marked invalid.

## Failure Behavior

I2C probe/read errors are shown on screen and in serial logs. Invalid BCD or out-of-range fields are shown as invalid data and are not treated as a pass.

## Hardware Steps

1. Build for target `esp32p4`.
2. Flash and open a serial monitor on real hardware.
3. Check that the display clock advances once per second.
4. Confirm GPIO3 INT level and oscillator/invalid status are shown.

No real-hardware validation was performed by this build-only task.
