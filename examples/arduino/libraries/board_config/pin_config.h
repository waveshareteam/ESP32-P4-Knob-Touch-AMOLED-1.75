#pragma once

// ESP32-P4-Knob-Touch-AMOLED-1.75 main board and knob adapter.
#define KNOB_PIN_BUTTON_BOOT 35
#define KNOB_PIN_BUTTON_PWR 2
#define KNOB_BUTTON_BOOT_ACTIVE_LEVEL 0
// The PWR switch reaches SYS_OUT through the BSS138 inverter.
#define KNOB_BUTTON_PWR_ACTIVE_LEVEL 1

#define KNOB_PIN_I2C_SDA 7
#define KNOB_PIN_I2C_SCL 8
#define KNOB_PIN_RTC_INT 3
#define KNOB_PIN_IMU_INT1 4
#define KNOB_PIN_IMU_INT2 5

#define KNOB_PIN_KNOB_CW 20
#define KNOB_PIN_KNOB_CCW 21
#define KNOB_PIN_HAPTIC_IN1 22
#define KNOB_PIN_HAPTIC_IN2 23

// Slot 0 uses the fixed SDMMC IO MUX. The core board variant controls card power.
#define KNOB_SDMMC_SLOT 0
#define KNOB_SDMMC_LDO_CHANNEL 4
#define KNOB_PIN_SD_CLK 43
#define KNOB_PIN_SD_CMD 44
#define KNOB_PIN_SD_D0 39
#define KNOB_PIN_SD_D1 40
#define KNOB_PIN_SD_D2 41
#define KNOB_PIN_SD_D3 42

#define KNOB_PIN_LCD_RESET 29
#define KNOB_PIN_TOUCH_RESET 27
#define KNOB_PIN_TOUCH_INT 26

#define KNOB_PIN_I2S_MCLK 13
#define KNOB_PIN_I2S_BCLK 12
#define KNOB_PIN_I2S_WS 10
#define KNOB_PIN_I2S_DOUT 9
#define KNOB_PIN_I2S_DIN 11
#define KNOB_PIN_PA_ENABLE 53
