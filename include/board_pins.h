#pragma once

/**
 * Waveshare ESP32-S3-Touch-LCD-1.85 pin map
 * https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.85
 *
 * EXIOn pins are on the onboard TCA9554PWR I2C expander.
 */

/* Display — ST77916 QSPI */
#define PIN_LCD_SDA0 46
#define PIN_LCD_SDA1 45
#define PIN_LCD_SDA2 42
#define PIN_LCD_SDA3 41
#define PIN_LCD_SCK  40
#define PIN_LCD_CS   21
#define PIN_LCD_TE   18
#define PIN_LCD_BL   5
#define EXIO_LCD_RST 2 /* TCA9554 */

/* Touch — CST816 */
#define PIN_TP_SDA 1
#define PIN_TP_SCL 3
#define PIN_TP_INT 4
#define EXIO_TP_RST 1 /* TCA9554 */

/* Speaker — PCM5101 I2S */
#define PIN_I2S_DOUT 47
#define PIN_I2S_LRCK 38
#define PIN_I2S_BCK  48

/* Mic */
#define PIN_MIC_WS  2
#define PIN_MIC_SCK 15
#define PIN_MIC_SD  39

/* Shared I2C (IMU QMI8658, RTC PCF85063, TCA9554) */
#define PIN_I2C_SCL 10
#define PIN_I2C_SDA 11
#define TCA9554_ADDR 0x20

/* SD / TF (SDMMC 1-bit; CS/D3 on expander) */
#define PIN_SD_MISO 16 /* D0 */
#define PIN_SD_MOSI 17 /* CMD */
#define PIN_SD_SCK  14
#define EXIO_SD_CS  3 /* TCA9554 EXIO3 */

#define DISPLAY_WIDTH  360
#define DISPLAY_HEIGHT 360
/* Keep chrome inside the round panel; top bias for vertical centering. */
#define DISPLAY_TOP_MARGIN    52
#define DISPLAY_BOTTOM_MARGIN 44
#define DISPLAY_SAFE_W 250
#define DISPLAY_NAV_H   36
#define DISPLAY_SAFE_H  (DISPLAY_HEIGHT - DISPLAY_TOP_MARGIN - DISPLAY_BOTTOM_MARGIN - DISPLAY_NAV_H - 10)
#define DISPLAY_SAFE_DIAMETER DISPLAY_SAFE_W /* legacy alias */
#define DISPLAY_EDGE_MARGIN DISPLAY_TOP_MARGIN /* legacy alias */
