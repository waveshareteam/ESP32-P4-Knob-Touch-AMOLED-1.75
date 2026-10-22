# 06_sd_card

SDMMC card mount and file round-trip test for ESP32-P4-Knob-Touch-AMOLED-1.75.

Hardware used:

- SDMMC D0/D1/D2/D3: GPIO39/GPIO40/GPIO41/GPIO42
- SDMMC CLK/CMD: GPIO43/GPIO44
- SD1_VDD GPIO45: left external/unchanged by default because the polarity and board power strategy are not confirmed here
- CO5300 466x466 display through the BSP component

Behavior:

- Mounts the card through the BSP (`bsp_sdcard_mount()`), 4-bit at 40 MHz on SDMMC slot 0.
- Mounts FATFS, prints card information, reports total/free space, writes a unique temporary file, reads it back, verifies the payload, deletes the file, and unmounts.
- Updates an LVGL status page through a queue while SD I/O runs in a worker task.

Internal pull-ups are enabled only as an auxiliary bring-up aid. Reliable SDMMC operation depends on proper external pull-ups on the SD bus.

Expected result:

- Serial log shows card information and a verified temporary file path.
- Display shows `Passed` and `Verified` when the full round-trip and cleanup succeed.

Still requires hardware verification with representative SD cards before using the result as a production storage qualification.
