# AI Dual-Eyed Doll V0.2

基于 ESP32-S3R8 的双眼玩偶板。当前目标为 `ai-dual-eyed-doll`，使用 16 MB Flash 和片内 Octal PSRAM。

## 硬件状态

- 双眼为两块 160x160、4 线 SPI GC9D01N 显示屏。
- ES8311 output Codec and ES7210 input Codec; two analog microphones connect to ES7210 MIC1 and MIC2. MIC3 is the AEC playback loopback reference channel.
- TCA9554 is populated. The two TTP233H touch outputs connect directly to ESP GPIO39/GPIO38; AHLB=0 and TOG=0, so they are active HIGH, not active low.
- IMU 和 SD 未焊接。固件仅将 TCA9554 全部端口配置为输入，不依赖 IMU 或 SD。
- R8 已由用户确认焊接/短接。

## 引脚与电源

引脚定义以 `config.h` 为准：

- I2C：SCL GPIO6，SDA GPIO7；TCA9554 地址 0x20。
- 音频 I2S：MCLK GPIO41，BCLK GPIO45，WS GPIO46，DOUT GPIO42，DIN GPIO40；功放使能 GPIO16。
- 电源键 KEY_ON：GPIO2，高电平按下、低电平释放（`POWER_BUTTON_ACTIVE_LEVEL=1`）。Q3 CJE3139K PMOS gate connects to GND through the button, source to 3.3 V, and drain through R51 to GPIO2; R53 supplies the external pull-down. GPIO and RTC internal pulls are disabled; ext0 wakes on HIGH.
- PowerEN1：GPIO15，低有效，控制屏幕电源轨。
- PowerEN2：GPIO18，高有效，控制音频电源轨。
- 运行 LED：GPIO1，仅运行指示，非电源锁存。
- KEY_1：GPIO47；KEY_2：GPIO48；Touch 1：GPIO39；Touch 2：GPIO38。
- 双屏 SPI：SCLK GPIO14、MOSI GPIO9、DC GPIO12、左/右 CS 分别为 GPIO10/GPIO13、RST GPIO11、背光 GPIO8。

共享 I2C 外部上拉接在常供电 rail。关机时固件将总线保持低电平，避免向已关闭的 Codec 电源轨反向供电；该状态约增加 1.4 mA 耗电，必须以实机测量确认。

## 音频与按键

音频输入配置为三通道、24 kHz，启用设备端 AEC；MMR Codec 新增设备 AEC 24 kHz 支持。运行时会将输入重采样为 16 kHz 后交给语音处理。

已确认的操作：

- 电源键：短按切换息屏/亮屏；长按 3 秒开机或关机。首次开机长按后的释放不会被当作关机操作。
- KEY_1：短按切换 AI 对话状态；长按进入或退出配网。
- KEY_2：短按和长按均保留，当前仅记录日志。
- Touch 1：按下显示开心表情并播放短笑声；Touch 2：按下显示生气表情并播放傲娇语音。TOG=0 时仅按下触发，不在释放时二次触发。

## 构建与分区

```bash
python scripts/build.py ai-dual-eyed-doll
```

使用原有 `partitions/v2/16m.csv`：NVS 为 16 KB（0x9000-0xCFFF），`otadata` 位于 0xD000。原始 NVS 备份为 `build/v02-original-nvs-COM33.bin`。

## 验证记录

2026-10-10 实机记录：

- ESP-IDF 5.5.4 build passed. The application image is 0x2591e0, leaving about 40% free in the application partition.
- 已通过 COM33 使用 `idf.py flash` 烧录，全部写入段的 Hash 校验通过。
- Startup took about 25 seconds. Logs confirmed 16 MB Flash, 8 MB PSRAM, TCA9554 (0x20), both GC9D01N panels, ES8311, ES7210, and MIC1/MIC2/MIC3 TDM initialized successfully; playback was enabled, with no crash or bus error.
- 用户已确认双眼均能正常显示；这是本记录中唯一已确认的实体交互结果。
- Wi-Fi can enter `Xiaozhi-CA31` provisioning. The old `wifi_provisioning` configuration store differs from the new program's Wi-Fi namespace; startup logs state that the new program has no saved Wi-Fi configuration, so it enters provisioning.

尚未验证：AI 对话、AEC 与麦克风采集；声音品质；触摸、按键与电源切换；电池与充电；以及深度睡眠功耗。这些项目仍需人工实机测试。
