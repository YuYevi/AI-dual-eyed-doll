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

操作说明：

- 电源键：短按切换息屏/亮屏；长按 3 秒开机或关机。首次开机长按后的释放不会被当作关机操作。
- KEY_1：短按切换 AI 对话状态；长按进入或退出配网。
- KEY_2：单击切换下一种动画，顺序为平静 → 开心 → 惊讶 → 生气 → 难过 → 困倦 → 害羞 → 害怕 → 平静；长按仍保留，仅记录日志。
- Touch 1：按下显示开心表情并播放短笑声；Touch 2：按下显示生气表情并播放傲娇语音。TOG=0 时仅按下触发，不在释放时二次触发。

## 构建与分区

```bash
python scripts/build.py ai-dual-eyed-doll
```

使用原有 `partitions/v2/16m.csv`：NVS 为 16 KB（0x9000-0xCFFF），`otadata` 位于 0xD000。原始 NVS 备份为 `build/v02-original-nvs-COM33.bin`。

当前双眼动画只使用 `docs/资源` 中每种表情的 `_L.gif`，包含平静、开心、惊讶、生气、难过、困倦、害羞、害怕八种表情。两块屏幕显示同一帧，右屏由显示控制器水平镜像，资源包只存一份动画。`scripts/pack_dual_eye_assets.py` 将动画转换为 RGB565 压缩差分帧，打包到 `assets/dual_eye_assets.pack`，并保留语音唤醒模型。

更换表情后必须烧录应用和 `assets` 分区，不能只烧录应用。构建会将资源包加入 `build/flash_args` 的 `0x800000` 项，可使用完整烧录命令：

```bash
idf.py -p COM33 flash
```

若启动日志出现 `dual_eye.anim is missing from the assets partition`，说明设备仍在使用旧资源分区，需要重新完整烧录。

## 验证记录

2026-10-12 单源动画与 KEY_2 调整：

- 每种表情改用一份 `_L.gif`，两屏共享帧，右屏水平镜像。
- KEY_2 单击循环切换八种动画；切换在应用线程调度，关机后不响应。
- 八种动画共 966 帧，逐帧解压重建后与源 GIF 的 RGB565 像素一致；单源资源包为 3,534,348 字节。
- ESP-IDF 5.5.4 编译通过，应用为 2,389,088 字节，应用分区剩余 42%；应用镜像校验和与 Hash 校验通过。
- 已生成完整分段固件包 `build/ai-dual-eyed-doll-single-resource-key2-20261012.zip`，包含应用、新资源包、启动程序、分区表及烧录说明。
- 用户要求本轮只生成固件，未烧录设备。
- 本轮镜像显示效果与 KEY_2 交互尚待实机确认。

2026-10-12 表情替换修复记录：

- 从 COM33 读回的旧资源分区仅包含 `srmodels.bin` 和 `index.json`，启动日志报 `dual_eye.anim is missing from the assets partition`。
- 新资源包的 1,932 个左右眼帧逐一解压、重建并与源 GIF 比对，RGB565 像素差异为 0。
- ESP-IDF 5.5.4 完整编译通过，应用为 2,388,816 字节，资源包为 5,198,170 字节；已通过 COM33 完整烧录，全部写入段 Hash 校验通过。
- 重启日志确认 `Emotion calm -> calm`，连续观察 35 秒未出现动画加载、解压、传输错误或崩溃。日志保存在 `build/dual_eye_flash.log` 和 `build/dual_eye_boot.log`。
- 用户已现场确认两块屏幕正常显示新表情动画，花屏问题已解决。
- 本地配置时使用进程级 `IDF_COMPONENT_CHECK_NEW_VERSION=0` 绕过组件管理器的新版本元数据检查问题，未更改组件清单或依赖锁文件。

2026-10-10 实机记录：

- ESP-IDF 5.5.4 build passed. The application image is 0x2591e0, leaving about 40% free in the application partition.
- 已通过 COM33 使用 `idf.py flash` 烧录，全部写入段的 Hash 校验通过。
- Startup took about 25 seconds. Logs confirmed 16 MB Flash, 8 MB PSRAM, TCA9554 (0x20), both GC9D01N panels, ES8311, ES7210, and MIC1/MIC2/MIC3 TDM initialized successfully; playback was enabled, with no crash or bus error.
- 用户已确认双眼均能正常显示；这是本记录中唯一已确认的实体交互结果。
- Wi-Fi can enter `Xiaozhi-CA31` provisioning. The old `wifi_provisioning` configuration store differs from the new program's Wi-Fi namespace; startup logs state that the new program has no saved Wi-Fi configuration, so it enters provisioning.

尚未验证：AI 对话、AEC 与麦克风采集；声音品质；触摸、按键与电源切换；电池与充电；以及深度睡眠功耗。这些项目仍需人工实机测试。
