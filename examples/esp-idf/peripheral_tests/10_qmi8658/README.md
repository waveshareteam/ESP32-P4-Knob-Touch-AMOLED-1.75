# 10_qmi8658

LVGL QMI8658 accelerometer chart test.

## Behavior

- Uses the BSP shared I2C bus through `bsp_i2c_init()`/`bsp_i2c_get_handle()`.
- Probes primary QMI8658 address 0x6B first, then alternate address 0x6A.
- Verifies `WHO_AM_I` is 0x05 before configuration.
- Enables register auto-increment/little-endian mode and configures accelerometer range for +/-8 g.
- Samples acceleration in an independent task at about 40 Hz; I2C is not performed in the LVGL timer loop.
- Updates the LVGL chart at about 10 Hz with fixed-color X/Y/Z lines and current g values.
- Shows connection state, active address, WHO_AM_I, and INT1/INT2 GPIO levels.

## Pass Conditions

- Status shows QMI8658 online and a valid WHO_AM_I value.
- X/Y/Z lines update smoothly on the fixed-size chart.
- Current values respond when the board orientation changes.
- I2C errors are shown as errors and the task attempts to recover.

## Failure Behavior

Probe, WHO_AM_I, configuration, or data-read failures are logged and shown on screen. On I2C failure the demo removes the I2C device, waits briefly, and retries instead of wedging the UI.

## Hardware Steps

1. Build for target `esp32p4`.
2. Flash and open a serial monitor on real hardware.
3. Rotate or tilt the board.
4. Confirm chart lines and numeric X/Y/Z readings change, and INT1/INT2 levels are shown.

No real-hardware validation was performed by this build-only task.
