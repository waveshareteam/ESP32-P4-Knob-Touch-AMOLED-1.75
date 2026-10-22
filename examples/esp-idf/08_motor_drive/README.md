# 08_motor_drive

Safety-limited DRV8837 haptic motor test with an LVGL press-and-hold button.

Hardware used:

- DRV8837 IN1: GPIO22
- DRV8837 IN2: GPIO23
- Idle contract: IN1 low, IN2 low
- Active contract: safe forward pulse only, IN1 high and IN2 low

Behavior:

- The LVGL button starts vibration on `PRESSED`.
- `RELEASED`, `PRESS_LOST`, `DELETE`, or the software timeout stops the motor.
- Default maximum on-time is 400 ms and can be configured up to 500 ms.
- Any GPIO or timer error immediately attempts IN1/IN2 low and reports `FAULT`.
- LVGL callbacks only enqueue commands; they do not block or delay.

Expected result:

- Pressing the button gives a short forward vibration.
- Releasing the button or holding longer than the guard time returns the UI to `IDLE`.

Still requires real-board verification of actuator feel, supply current, and thermal behavior.
