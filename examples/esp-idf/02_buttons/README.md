# 02_buttons

Read-only BOOT and PWR/GPIO2 input test.

## Behavior

- GPIO35 (`BOARD_BOOT_GPIO`) and GPIO2 (`BOARD_PWR_GPIO`) are configured as independent inputs.
- The demo uses the existing validated input behavior from the old `keys_power` and `pwr_gpio2` tests: internal pull-up enabled, active level LOW.
- No output mode is enabled and neither pin is driven.
- Startup logs the current level of both pins.
- Debounced transitions print `PRESS` or `RELEASE` with a per-button press count.

## Pass Conditions

- Serial log shows both startup levels.
- Pressing and releasing each physical button produces the matching `PRESS` and `RELEASE` lines.
- Counts increment only on `PRESS` events.

## Failure Behavior

GPIO configuration failures are logged as `FAIL` and the demo stops before polling. Missing or noisy transitions remain visible in the serial log because raw startup level and debounced changes are printed.

## Hardware Steps

1. Build for target `esp32p4`.
2. Flash and open a serial monitor on real hardware.
3. Press BOOT and PWR/GPIO2 separately.
4. Confirm GPIO35 and GPIO2 report independent active-low transitions.

No real-hardware validation was performed by this build-only task.
