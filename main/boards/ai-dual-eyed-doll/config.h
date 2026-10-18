#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <hal/adc_types.h>

// 音频采样率与回声消除参考输入
#define AUDIO_INPUT_SAMPLE_RATE 24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_INPUT_REFERENCE true
#define AUDIO_INPUT_CHANNELS 2 // MIC1 + MIC3 回声参考
#define AUDIO_CODEC_ES8311_ADDR ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR 0x82

// 共享 I2C 引脚、音频编解码器总线与扩展器
#define I2C_SCL_IO GPIO_NUM_6
#define I2C_SDA_IO GPIO_NUM_7
#define AUDIO_CODEC_I2C_PORT I2C_NUM_0
#define TCA9554_I2C_ADDRESS 0x20

// 音频 I2S 引脚与功放使能
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_41
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_45
#define AUDIO_I2S_GPIO_WS GPIO_NUM_46
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_42
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_40
#define AUDIO_PA_ENABLE_GPIO GPIO_NUM_16

// ML307 串口通信
// 主控发送引脚接模组接收引脚；主控接收引脚接模组发送引脚。模组独立上电。
#define ML307_TX_PIN GPIO_NUM_43
#define ML307_RX_PIN GPIO_NUM_44
#define ML307_DTR_PIN GPIO_NUM_NC

// 电源控制与电源按键时序
#define PWR_BUTTON_GPIO GPIO_NUM_2
#define POWER_BUTTON_ACTIVE_LEVEL 1 // Q3 PMOS：按下时为高电平；松开时由外部 R53 保持低电平
#define POWER_EN1_GPIO GPIO_NUM_15 // Q1 PMOS：低电平使能屏幕电源
#define POWER_EN2_GPIO GPIO_NUM_18 // 音频 DCDC：高电平使能电源
#define RUN_LED_GPIO GPIO_NUM_1 // 仅用作运行指示 LED；不可用作电源锁存
#define POWER_BUTTON_HOLD_MS 3000
#define POWER_BUTTON_POLL_MS 20

// 功能按键
#define KEY_1_GPIO GPIO_NUM_47
#define KEY_2_GPIO GPIO_NUM_48

// 电池与 USB 电压采样、充电状态
#define FULL_STATUS_GPIO GPIO_NUM_17
#define FULL_STATUS_ACTIVE_LEVEL 0
#define BATTERY_ADC_GPIO GPIO_NUM_4
#define BATTERY_ADC_UNIT ADC_UNIT_1
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#define USB_ADC_GPIO GPIO_NUM_5
#define USB_ADC_CHANNEL ADC_CHANNEL_4

// 触摸输入
#define TOUCH_1_GPIO GPIO_NUM_39
#define TOUCH_2_GPIO GPIO_NUM_38
#define TOUCH_ACTIVE_HIGH true // AHLB 接地；实装 TOG 接地（非自锁模式）

// 显示尺寸、方向与像素格式
#define DISPLAY_WIDTH 160
#define DISPLAY_HEIGHT 160
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_OFFSET_X 0
#define DISPLAY_OFFSET_Y 0
#define DISPLAY_INVERT_COLOR false
#define DISPLAY_BITS_PER_PIXEL 16

// 显示 SPI 总线与双屏控制引脚
#define DISPLAY_SPI_HOST SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define DISPLAY_SPI_SCLK_GPIO GPIO_NUM_14
#define DISPLAY_SPI_MOSI_GPIO GPIO_NUM_9
#define DISPLAY_SPI_DC_GPIO GPIO_NUM_12
#define DISPLAY_1_SPI_CS_GPIO GPIO_NUM_10
#define DISPLAY_2_SPI_CS_GPIO GPIO_NUM_13
#define DISPLAY_SPI_RST_GPIO GPIO_NUM_11

// 背光控制
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_8
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false // Q4 对 LEDK 电平反相，但栅极为高电平时背光点亮
#define DISPLAY_BACKLIGHT_OFF_LEVEL 0 // Q4 栅极为低电平时，晶体管关断

#endif
