#pragma once

// Per-board defines live here instead of boards/*.json extra_flags. Each -D on the
// command line adds to the Windows 32,767-character limit on the compiler call, so
// the JSON keeps only the framework flags and this header is pulled in with -include.

#if defined(SYD_V1_0_0)

#define WIFI_LED_R_PIN P0_5
#define WIFI_LED_G_PIN P0_4
#define SERVER_LED_R_PIN P0_7
#define SERVER_LED_G_PIN P0_6
#define POWER_LED_R_PIN P0_3
#define BRIGHTNESS_DOWN_BUTTON P0_2
#define BRIGHTNESS_UP_BUTTON P0_1
#define POWER_BUTTON 8
#define MODE_BUTTON P0_0
#define LED_POWER_CH1_EN P1_3
#define LED_POWER_CH2_EN P1_4
#define LED_1_PIN 4
#define LED_1_START 100
#define LED_1_PIXELS 198
#define LED_2_PIN 6
#define LED_2_START 300
#define LED_2_PIXELS 171
#define LED_3_PIN 7
#define LED_3_START 500
#define LED_3_PIXELS 144
#define LED_4_PIN 5
#define LED_4_START 700
#define LED_4_PIXELS 179
#define LED_5_PIN 0
#define LED_5_START 900
#define LED_5_PIXELS 97
#define MIN_BRIGHTNESS 64
#define MAX_BRIGHTNESS 254
#define BRIGHTNESS_STEP 1
#define BRIGHTNESS_HYSTERESIS 3
#define DEBOUNCE_MS 50
#define GPIO_EXPANDER AW9523B
// LED current mode dim values (0-255) for the status LEDs. Full scale is set in gpioExpander.cpp.
// Red 0.54 mA and green 0.6 mA look equally bright to the eye.
#define STATUS_LED_RED_DIM 15
#define STATUS_LED_GREEN_DIM 17
#define SCL_PIN 10
#define SDA_PIN 2
#define I2C_INT_PIN 9
#define BACKEND_VERSION "100"
#define CITY_CODE "syd"
#define TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"

#elif defined(AKL_V1_0_0) || defined(AKL_V1_1_0)

#if defined(AKL_V1_0_0)
#define WIFI_LED_PIN 10
#define SERVER_LED_PIN 5
#define LED_1_PIXELS 108
#define LED_2_PIXELS 44
#define BACKEND_VERSION "100"
#else
#define WIFI_LED_PIN 5
#define SERVER_LED_PIN 10
#define LED_1_PIXELS 109
#define LED_2_PIXELS 45
#define BACKEND_VERSION "110"
#endif
#define BRIGHTNESS_DOWN_BUTTON 6
#define BRIGHTNESS_UP_BUTTON 7
#define POWER_BUTTON 9
#define LED_5V_EN 4
#define LVL_Shifter_EN 0
#define LED_1_PIN 3
#define LED_1_START 100
#define LED_2_PIN 1
#define LED_2_START 300
#define MIN_BRIGHTNESS 39
#define MAX_BRIGHTNESS 239
#define BRIGHTNESS_STEP 20
#define DEBOUNCE_MS 50
#define TIMETABLE_SPEED 600.0
#define CITY_CODE "akl"
#define OUT_OF_SERVICE_TRAINS
#define TIMEZONE "NZST-12NZDT,M9.5.0,M4.1.0/3"

#elif defined(WLG_V1_0_0)

#define WIFI_LED_PIN 5
#define SERVER_LED_PIN 4
#define BRIGHTNESS_DOWN_BUTTON 10
#define BRIGHTNESS_UP_BUTTON 1
#define POWER_BUTTON 9
#define MODE_BUTTON 8
#define LED_5V_EN 6
#define LED_1_PIN 7
#define LED_1_START 100
#define LED_1_PIXELS 235
#define MIN_BRIGHTNESS 6
#define MAX_BRIGHTNESS 254
#define BRIGHTNESS_STEP 20
#define BRIGHTNESS_HYSTERESIS 3
#define DEBOUNCE_MS 50
#define LIGHT_SENSOR 1
#define SCL_PIN 3
#define SDA_PIN 2
#define TIMETABLE_SPEED 1000.0
#define BACKEND_VERSION "100"
#define CITY_CODE "wlg"
#define TIMEZONE "NZST-12NZDT,M9.5.0,M4.1.0/3"

#elif defined(MEL_V1_0_0)

#define WIFI_LED_PIN 10
#define SERVER_LED_PIN 11
#define BRIGHTNESS_DOWN_BUTTON 7
#define BRIGHTNESS_UP_BUTTON 8
#define POWER_BUTTON 0
#define MODE_BUTTON 9
#define LVL_Shifter_EN 48
#define LED_POWER_CH1_EN 42
#define LED_POWER_CH2_EN 41
#define LED_1_PIN 37
#define LED_1_START 100
#define LED_1_PIXELS 217
#define LED_2_PIN 33
#define LED_2_START 400
#define LED_2_PIXELS 150
#define LED_3_PIN 47
#define LED_3_START 600
#define LED_3_PIXELS 154
#define LED_4_PIN 35
#define LED_4_START 800
#define LED_4_PIXELS 147
#define LED_5_PIN 38
#define LED_5_START 1000
#define LED_5_PIXELS 117
#define LED_6_PIN 39
#define LED_6_START 1200
#define LED_6_PIXELS 160
#define LED_7_PIN 34
#define LED_7_START 1400
#define LED_7_PIXELS 75
#define LED_8_PIN 36
#define LED_8_START 1600
#define LED_8_PIXELS 22
#define MIN_BRIGHTNESS 6
#define MAX_BRIGHTNESS 192
#define BRIGHTNESS_STEP 50
#define BRIGHTNESS_HYSTERESIS 3
#define DEBOUNCE_MS 50
#define LIGHT_SENSOR 1
#define SCL_PIN 16
#define SDA_PIN 15
#define TIMETABLE_SPEED 600.0
#define BACKEND_VERSION "100"
#define CITY_CODE "mel"
#define TIMEZONE "AEST-10AEDT,M10.1.0,M4.1.0/3"

#endif
