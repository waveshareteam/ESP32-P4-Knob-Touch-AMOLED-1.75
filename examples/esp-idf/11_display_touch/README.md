# 11_display_touch

Combined CO5300 display color-stage and CST9217 touch drawing test.

Hardware used:

- Display: CO5300, 466x466
- Touch: CST9217
- LCD reset: GPIO29
- Touch reset: GPIO27

Behavior:

- Starts the verified BSP LVGL display path once with independent LCD and touch reset lines.
- Uses LVGL full-screen fills for red, green, and blue on that same BSP display.
- Each color stage lasts about 1.8 seconds.
- A panel touch or BOOT button press can advance a color stage early.
- After the color stages, the same LVGL display switches to a white background.
- Touch input draws fixed-size black dots at bounded coordinates.
- A small `Clear` button removes dots and resets the counter.

Pass criteria:

- Red, green, and blue full-screen phases are clean, correctly colored, and complete automatically.
- The white touch page appears after the color stages.
- Touch points appear where the panel is touched, stay inside the visible screen area, and the clear button resets the count.

LCD reset GPIO29 and touch reset GPIO27 are independent reset pins.
