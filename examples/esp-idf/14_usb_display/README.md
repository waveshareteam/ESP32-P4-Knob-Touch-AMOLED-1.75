# 14_usb_display

Reserved ESP-IDF project skeleton for a future USB display test.

Current status:

- RESERVED
- NOT IMPLEMENTED
- UNTESTED

This project intentionally does not initialize:

- USB host or USB device roles
- Host 5 V or external USB power switching
- TinyUSB
- DisplayLink or other USB display protocols
- Any unknown expansion-screen driver

Future implementation must first confirm:

- The intended USB role for the ESP32-P4 side.
- The board power path and whether any Host 5 V control GPIO exists.
- The USB display protocol or bridge IC.
- The exact external display model and resolution.
- Whether the test should share the onboard CO5300 UI for status only or target a real USB display endpoint.

The firmware reports only RESERVED / NOT IMPLEMENTED / UNTESTED status because no USB display behavior is implemented or tested yet.

