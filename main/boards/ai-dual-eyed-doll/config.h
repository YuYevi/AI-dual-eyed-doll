#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <hal/adc_types.h>

/*
 * Pin map is taken from docs/原理图与外设手册/Netlist_Schematic1_2026-08-25.tel
 * together with the ESP32-S3 QFN-56 datasheet. The EasyEDA ESP32-S3R8 symbol
 * labels pins 38-52 two GPIO numbers low (pin 38 is drawn as "GPIO33" but is
 * silicon GPIO35). Firmware uses the datasheet numbers.
 *
 * Hardware rework vs the 2026-08-25 netlist:
 *   - KEY_2 removed, PA_SD moved to GPIO18, R44 DNP
 *   - Touch_out3 / Touch_out4 not enabled
 *
 * ESP32-S3R8 in-package octal PSRAM occupies GPIO33-37. Do not mux those pads.
 */

/* ==================== Audio (ES8390, driven as ES8389) ==================== */
#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE false
#define AUDIO_INPUT_CHANNELS 2

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_47  /* U4.51 I2S_MCLK */
#define AUDIO_I2S_GPIO_WS GPIO_NUM_48    /* U4.52 I2S_WS / LRCK */
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_43  /* U4.47 I2S_SCK */
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_44  /* U4.48 I2S_DO -> ES8390 DSDIN */
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_42   /* U4.45 I2S_DI_CD <- ES8390 ASDOUT */

#define AUDIO_CODEC_I2C_SCL_PIN GPIO_NUM_6  /* U4.11 TP_SCL, 4.7k to VCCIO_CODEC */
#define AUDIO_CODEC_I2C_SDA_PIN GPIO_NUM_7  /* U4.12 TP_SDA, 4.7k to VCCIO_CODEC */
#define AUDIO_CODEC_ES8389_ADDR 0x20        /* ES8390 AD0 tied to GND via R27 */
#define AUDIO_CODEC_PA_PIN GPIO_NUM_18      /* PA_SD -> LM4890 SHUTDOWN#, active-low amp */
#define AUDIO_CODEC_PA_INVERTED 0

/* ==================== Buttons / power / touch ==================== */
#define BOOT_BUTTON_GPIO GPIO_NUM_0   /* SW1, 10k pull-up, active-low */
#define KEY_1_BUTTON_GPIO GPIO_NUM_17 /* CN8 KEY_1, 51k pull-up, ESD, active-low */
#define PWR_BUTTON_GPIO GPIO_NUM_2    /* KEY_ON / CN10, 51k pull-up, active-low */
#define PWR_CONTROL_PIN GPIO_NUM_1    /* PWR_CTRL, high holds the power latch */
#define POWER_BUTTON_HOLD_MS 3000
#define POWER_BUTTON_POLL_MS 20
#define CHARGE_DETECT_PIN GPIO_NUM_16 /* CHG_STA, 51k pull-up, STAT active-low */
#define TOUCH_1_GPIO GPIO_NUM_8       /* Touch_out1, TTP233H U2 Q, AHLB open = active-high */
#define TOUCH_2_GPIO GPIO_NUM_40      /* Touch_out2, TTP233H U7 Q */

#define POWER_BATTERY_ADC_UNIT ADC_UNIT_1
#define POWER_BATTERY_ADC_CHANNEL ADC_CHANNEL_3 /* GPIO4 ADC_BAT, 200k/200k divider */

/* ==================== Dual GC9D01N eyes (0.71", 160x160, 4-SPI) ==================== */
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 160
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_INVERT_COLOR false

#define DISPLAY_SPI_SCK_PIN GPIO_NUM_9    /* SPI_SCLK1 shared */
#define DISPLAY_SPI_MOSI_PIN GPIO_NUM_10  /* SPI_MOSI1 shared */
#define DISPLAY_DC_PIN GPIO_NUM_11        /* SPI_DC1 shared */
#define DISPLAY_LEFT_CS_PIN GPIO_NUM_12   /* SPI_CS1 / FPC1 */
#define DISPLAY_RIGHT_CS_PIN GPIO_NUM_13  /* SPI_CS2 / FPC2, MADCTL mirror */
#define DISPLAY_RST_PIN GPIO_NUM_14       /* SPI_RST1 shared */
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_41 /* TFT_BL_PWM1 / U4.44 MTCK */
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define DISPLAY_SPI_HOST SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ (40 * 1000 * 1000)

#endif  // _BOARD_CONFIG_H_
