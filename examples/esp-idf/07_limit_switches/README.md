# 07_limit_switches

Diagnostic test for the two independent limit switch inputs with an LVGL status
display.

The default build is a polling test: GPIO20 and GPIO21 are configured as digital
inputs with internal pull-ups and read every 10 ms. GPIO interrupts and PCNT
remain disabled. The display shows each sampled level and falling-edge count.

## Behavior

- The `Diagnostic mode` Kconfig choice provides input-only, polling and PCNT
  stages. `CONFIG_LIMIT_SWITCHES_MODE_POLLING` is the current default.
- GPIO20 and GPIO21 are treated as independent switch inputs.
- The two inputs are independent switches with no direction or A/B interpretation.
- Default GPIO bias is internal pull-up. The on-board switch circuit pulls the input to GND when triggered, so an idle switch reads HIGH and a triggered one reads LOW. Use the Kconfig choice only if different external hardware requires another bias.
- Current test hardware uses C69/C70 not populated and C7/C8 fitted with 2.2 nF.
- Polling mode reports the sampled level, falling edges and all transitions once per second.
- The display uses 0-degree rotation and triple-partial buffering.
- GPIO tasks publish atomic state snapshots without calling LVGL or taking the display lock.
- A 100 ms timer owned by the LVGL worker updates the HIGH/LOW and trigger-count labels.
  Repeated GPIO changes are coalesced, and unchanged labels are not invalidated.
- Panel backgrounds remain fixed so a state change does not force a full-panel redraw.
- The unused touch input is unregistered after BSP display startup, so LVGL does not poll it.
- The LVGL tick period is 10 ms and the worker delay is clamped to 20-50 ms. This avoids the
  previous 1 ms adapter tick and one-FreeRTOS-tick worker wakeups.
- The FreeRTOS SYSTIMER tick interrupt uses Level 3 instead of Level 1. This lets the kernel
  tick preempt lower-priority display/DMA2D interrupt activity rather than missing the
  interrupt-watchdog feed while both CPUs are idle.
- The 16 KB LVGL task stack is kept in internal RAM instead of optional PSRAM.
- The LVGL worker remains pinned to CPU0 for comparison with the previous dynamic UI build.
- Polling is a stability test and can miss a pulse shorter than the 10 ms sample interval.
- PCNT hardware captures each LOW pulse without requiring it to remain LOW for a polling or debounce interval.
- The debounce setting only suppresses repeated falling edges from the same switch inside the configured window.
- The input-only mode prints `INPUT-ONLY heartbeat` once per second and does not read the pins.
- The PCNT mode reports current GPIO levels plus accepted and raw falling-edge counts once per second.
- PSRAM remains enabled at 200 MHz, but instruction and read-only-data XIP from PSRAM are disabled during this GPIO/display diagnostic test.
- Panic frame pointers and UART core dumps are enabled so a watchdog reset retains diagnostic information.

## Pass Conditions

- `POLL STATUS` continues once per second during aggressive ring movement.
- GPIO20 and GPIO21 levels follow switch activation.
- Each sampled HIGH-to-LOW transition increments the corresponding falling count.
- The on-screen HIGH/LOW values and trigger counters follow the sampled GPIO state.
- Startup prints `UI mode=dynamic core=0 touch=0 refresh=100 ms` and
  `systimer_level=3 runtime_bsp_lock=0 full_panel_redraw=0`.
- No interrupt-watchdog panic, hang or reset occurs.

## Failure Behavior

GPIO initialization failures are logged as `FAIL`. If a switch input floats because the external circuit is not connected and no bias is selected, the serial log may show unstable triggers; that is a wiring or bias issue, not a pass.

## Hardware Steps

1. Verify C69/C70 are not populated and C7/C8 are 2.2 nF.
2. Build for target `esp32p4`, flash and open a serial monitor.
3. Rapidly move the ring while checking that `POLL STATUS` continues.
4. After polling mode passes, select `CONFIG_LIMIT_SWITCHES_MODE_PCNT` and test hardware edge counting.
