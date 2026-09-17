#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <hal/adc_types.h>

/*
 * 编号说明：
 * - U4.x / U6.x / FPCx.x 中的 x 是元件封装引脚（焊盘）编号，不是 GPIO 编号。
 * - GPIO_NUM_x 中的 x 才是 ESP32-S3 的 GPIO 编号。
 * - 下列映射由原理图网表的网络连接与 ESP32-S3R8 QFN-56 管脚表交叉确认。
 */

/* ==================== 音频：ES8390（U6） ==================== */
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE    false
#define AUDIO_INPUT_CHANNELS     2       // 两个模拟麦克风分别接入 ES8390 的两路 ADC

#define AUDIO_CODEC_I2C_PORT     I2C_NUM_0
#define AUDIO_CODEC_I2C_ADDRESS  0x20    // esp_codec_dev 使用包含 R/W 位的 8 位地址；U6.26（AD0）接地
#define I2C_SCL_IO               GPIO_NUM_6  // TP_SCL：U4.11（封装脚 11）→ GPIO6；连接 U6.28（CCLK）
#define I2C_SDA_IO               GPIO_NUM_7  // TP_SDA：U4.12（封装脚 12）→ GPIO7；连接 U6.27（CDATA）

#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_45 // I2S_MCLK：U4.51（封装脚 51）→ GPIO45；连接 U6.1
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_41 // I2S_SCK：U4.47（封装脚 47）→ GPIO41；连接 U6.5
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_46 // I2S_WS：U4.52（封装脚 52）→ GPIO46；连接 U6.7
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_42 // I2S_DO：U4.48（封装脚 48）→ GPIO42；连接 U6.6（DSDIN）
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_40 // I2S_DI_CD：U4.45（封装脚 45）→ GPIO40；连接 U6.8（ASDOUT）
#define AUDIO_PA_ENABLE_GPIO     GPIO_NUM_18 // 最新硬件改板：PA_SD 改接 GPIO18；旧网表 U4.40→GPIO35 已停用，R44 已去除
#define AUDIO_PWM_GPIO           GPIO_NUM_3  // AUDIO_PWM：U4.8（封装脚 8）→ GPIO3


/* ==================== 按键、电源、触摸与检测信号 ==================== */
#define BOOT_BUTTON_GPIO         GPIO_NUM_0  // IO0：U4.5（封装脚 5）→ GPIO0；SW1 按下接地，低有效
#define PWR_BUTTON_GPIO          GPIO_NUM_2  // KEY_ON：U4.7（封装脚 7）→ GPIO2；外部 51kΩ 上拉，低有效
#define PWR_CONTROL_GPIO         GPIO_NUM_1  // PWR_CTRL：U4.6（封装脚 6）→ GPIO1；电源锁存控制
#define PWR_Control_PIN          PWR_CONTROL_GPIO
#define POWER_BUTTON_HOLD_MS     3000    // 电源键长按 3 秒开机或关机
#define POWER_BUTTON_POLL_MS     20      // 开机长按确认轮询周期
#define KEY_1_GPIO               GPIO_NUM_17 // KEY_1：U4.23（封装脚 23）→ GPIO17；外部 51kΩ 上拉，低有效
#define KEY_2_GPIO               GPIO_NUM_NC // 最新硬件已去除 KEY_2；GPIO18 现用于 PA_SD

#define CHARGE_STATUS_GPIO       GPIO_NUM_16 // CHG_STA：U4.22（封装脚 22）→ GPIO16；U1.9（STAT）
#define BATTERY_ADC_GPIO         GPIO_NUM_4  // ADC_BAT：U4.9（封装脚 9）→ GPIO4（ADC1_CH3）
#define BATTERY_ADC_UNIT         ADC_UNIT_1
#define BATTERY_ADC_CHANNEL      ADC_CHANNEL_3
#define EXTERNAL_INTERRUPT_GPIO  GPIO_NUM_21 // EX_INT：U4.27（封装脚 27）→ GPIO21；GPIO20 是 USB D+

#define TOUCH_1_GPIO             GPIO_NUM_8  // Touch_out1：U4.13（封装脚 13）→ GPIO8；来自 U2.1
#define TOUCH_2_GPIO             GPIO_NUM_38 // Touch_out2：U4.43（封装脚 43）→ GPIO38；来自 U7.1
#define TOUCH_3_GPIO             GPIO_NUM_NC // 不启用：原 Touch_out3 所在 GPIO37 由 ESP32-S3R8 PSRAM 占用
#define TOUCH_4_GPIO             GPIO_NUM_NC // 不启用：原 Touch_out4 所在 GPIO36 由 ESP32-S3R8 PSRAM 占用

/* ==================== 双 GC9D01 显示屏：160 × 160，4 线 SPI ==================== */
#define DISPLAY_WIDTH            160
#define DISPLAY_HEIGHT           160
#define DISPLAY_MIRROR_X         false
#define DISPLAY_MIRROR_Y         false
#define DISPLAY_SWAP_XY          false
#define DISPLAY_OFFSET_X         0
#define DISPLAY_OFFSET_Y         0
#define DISPLAY_INVERT_COLOR     false
#define DISPLAY_BITS_PER_PIXEL   16

#define DISPLAY_SPI_HOST         SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ     (40 * 1000 * 1000)

// 两块屏共用 SCLK、MOSI、D/C 和 RESET，仅片选 CS 独立。
#define DISPLAY_SPI_SCLK_GPIO    GPIO_NUM_9  // SPI_SCLK1：U4.14 → GPIO9；FPC1.9/FPC2.9（SCL）
#define DISPLAY_SPI_MOSI_GPIO    GPIO_NUM_10 // SPI_MOSI1：U4.15 → GPIO10；FPC1.10/FPC2.10（SDA）
#define DISPLAY_SPI_DC_GPIO      GPIO_NUM_11 // SPI_DC1：U4.16 → GPIO11；FPC1.7/FPC2.7（D/C）
#define DISPLAY_1_SPI_CS_GPIO    GPIO_NUM_12 // SPI_CS1：U4.17 → GPIO12；FPC1.8（CS，低有效）
#define DISPLAY_2_SPI_CS_GPIO    GPIO_NUM_13 // SPI_CS2：U4.18 → GPIO13；FPC2.8（CS，低有效）
#define DISPLAY_SPI_RST_GPIO     GPIO_NUM_14 // SPI_RST1：U4.19 → GPIO14；FPC1.11/FPC2.11（RESET，低有效）

// R71（0Ω）连接 SPI_BL1 与 SPI_BL2，两块屏共用同一路背光 PWM。
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_39 // TFT_BL_PWM1：U4.44（封装脚 44）→ GPIO39
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif // _BOARD_CONFIG_H_
