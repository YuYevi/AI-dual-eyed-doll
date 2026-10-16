# AI Dual-Eyed Doll V0.2

基于 ESP32-S3R8 的双眼玩偶板。当前目标为 `ai-dual-eyed-doll`，使用 16 MB Flash 和片内 Octal PSRAM。

## 硬件状态

- 双眼为两块 160x160、4 线 SPI GC9D01N 显示屏，均不使用水平镜像，分别显示左、右眼原始动画。
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

音频实现参考[上游 SZPI 板](https://github.com/78/xiaozhi-esp32/blob/main/main/boards/lckfb/szpi-esp32s3/lichuang_dev_board.cc)，复用官方 `BoxAudioCodec`、ES7210、ES8311 和 `esp_codec_dev`。本板在板级适配功放 GPIO16，并配置为双麦 MMR：MIC1、MIC2 为人声，MIC3 为扬声器回声参考；采样率为 24 kHz，AI 输入重采样为 16 kHz，启用双麦语音增强和设备端 AEC。

ES7210 的 TDM 顺序为 MIC1、MIC3、MIC2、MIC4。驱动选择掩码 `0x7`，再将数据重排为 MIC1、MIC2、MIC3 参考。AI 使用 `AFE_TYPE_SR`、`AEC_MODE_SR_HIGH_PERF` 和 SE 处理双麦；VC 模式只支持单麦，不能用于本板双麦阵列。公共录放测试当前仅回放 MIC1，不能据此验证 MIC2 是否参与 AI 处理。

本板的 GPIO40（I²S DIN）必须保持纯输入、无上下拉；板级已在上电、外设初始化和关机流程中处理。原自定义 `DollAudioCodec` 已移除，公共驱动的默认参数仍兼容其他单麦板型。

用户已确认本地录放及关机重启后录放正常。AI 双麦初始化应检查串口 `2MIC`、`2 microphone` 和 SE 流程，AI 对话及 AEC 效果仍需实机验收。

固件操作映射：

- 电源键：短按切换息屏/亮屏；长按 3 秒开机或关机。首次开机长按后的释放不会被当作关机操作。
- KEY_1：短按切换 AI 对话状态；仅在 WiFi 模式下长按 2 秒进入或退出配网，4G 模式下长按不触发配网。退出配网时通过重启停止配网 AP 并重新连接已保存的 WiFi；没有已保存 SSID 时会重新进入配网。
- KEY_2：短按从当前表情切换到下一种动画，按平静 → 开心 → 惊讶 → 生气 → 难过 → 困倦 → 害羞 → 害怕 → 平静循环；长按 2 秒切换 WiFi / 4G，保存选择并重启。
- Touch 1：按下显示开心表情并播放短笑声；Touch 2：按下显示生气表情并播放傲娇语音。TOG=0 时仅按下触发，不在释放时二次触发。

## ML307C 4G 与 WiFi 切换

- 首次启动且 NVS 没有网络选择记录时，默认使用 ML307C 4G。沿用公共 `DualNetworkBoard`：NVS 命名空间 `network` 的 `type=1` 表示 4G、`type=0` 表示 WiFi；已有选择优先于默认值。
- KEY_2 长按 2 秒会保存另一种网络类型，显示切换提示，约 2 秒后重启（网络切换和应用重启各等待 1 秒，关闭音频可能使等待更长）；下次启动使用保存的类型。切换不清除 WiFi 凭据，4G 检测失败也不会自动回退至 WiFi。
- 模块串口交叉连接：ESP32-S3 GPIO43（TX）接 ML307C RX，GPIO44（RX）接 ML307C TX，双方共地。使用现有 AT 模组驱动，检测支持包含 921600 在内的波特率，通信目标波特率为 921600。
- DTR 未连接（`GPIO_NUM_NC`），PWRKEY 也不由固件控制；假设 ML307C 有独立且充足的供电并可自行开机。玩偶的软件关机不代表模块供电关闭。
- GPIO43/44 不再用于应用 UART0 控制台。构建配置将主控制台改为 USB Serial/JTAG，关闭第二控制台；使用 ESP32-S3 原生 USB 接口查看日志。板级编译检查会拒绝不符合该控制台配置的构建，避免控制台占用模组引脚。

官方上游实现参考：[双网络切换与持久化（dual_network_board.cc）](https://github.com/78/xiaozhi-esp32/blob/main/main/boards/common/dual_network_board.cc)、[ML307 模组初始化与通信（ml307_board.cc）](https://github.com/78/xiaozhi-esp32/blob/main/main/boards/common/ml307_board.cc)。

MQTT 音频必须在释放通道锁后发送。ML307 的 AT 确认与下行音频共用串口解析任务，持锁等待发送确认会阻塞下行回调，直到发送超时。发送期间通过共享所有权保持 UDP 对象有效，避免并发关闭时提前销毁；无需调整驱动发送超时或修改 ESP-IDF 组件。

## 构建与分区

```bash
python scripts/build.py ai-dual-eyed-doll
```

使用原有 `partitions/v2/16m.csv`：NVS 为 16 KB（0x9000-0xCFFF），`otadata` 位于 0xD000。原始 NVS 备份为 `build/v02-original-nvs-COM33.bin`。

当前双眼动画来自 `docs/资源` 中的左右眼 GIF，包含平静、开心、惊讶、生气、难过、困倦、害羞、害怕八种表情。`scripts/pack_dual_eye_assets.py` 将动画转换为 RGB565 压缩差分帧，打包到 `assets/dual_eye_assets.pack`，并保留语音唤醒模型。

更换表情后必须烧录应用和 `assets` 分区，不能只烧录应用。构建会将资源包加入 `build/flash_args` 的 `0x800000` 项，可使用完整烧录命令：

```bash
idf.py -p COM33 flash
```

若启动日志出现 `dual_eye.anim is missing from the assets partition`，说明设备仍在使用旧资源分区，需要重新完整烧录。

## 验证记录

2026-10-12 KEY_2 与屏幕方向调整记录：

- KEY_2 单击从当前表情切换到下一种动画；右屏取消水平镜像，显示右眼原始动画。
- ESP-IDF 5.5.4 编译通过，应用为 2,389,248 字节；已通过 COM33 完整烧录，全部五个写入段 Hash 校验通过。
- 35 秒启动日志确认 `Emotion calm -> calm`，未出现动画错误或崩溃；日志记录了 8 次实际 KEY_2 单击，依次切换开心、惊讶、生气、难过、困倦、害羞、害怕并回到平静，完整循环通过实机串口验证。
- 日志保存在 `build/key2_flash.log` 和 `build/key2_boot.log`。右屏取消镜像后的视觉方向仍待用户现场确认。

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

尚未验证：AI 对话、AEC 与麦克风采集；声音品质；触摸、其他按键与电源切换；电池与充电；以及深度睡眠功耗。这些项目仍需人工实机测试。

## 启动花屏原因与修复（2026-10-12）

本次处理的是开机先短暂出现花屏、随后正常播放的问题。此前资源分区修复解决的是动画资源缺失，并未保证启动阶段始终屏蔽未初始化的显存。

根因是面板开启显示早于显存初始化，并伴随双屏共享 SPI 的片选时序错误：

- `docs/原理图与外设手册/GC9D01N DataSheet V1.1.pdf` 第 120 页说明，上电时显存内容随机，软件复位和硬件复位都不会清空显存。第 115 页说明 `0x29`（Display ON）直接启用显存输出；第 114 页说明 `0x28`（Display OFF）屏蔽显存输出并插入空白画面。
- 旧 `ai-dual-eyed-doll.cc` 的 `kGc9d01InitCommands` 在末尾发送 `0x29`，随后发送没有像素数据的 `0x2C`（Memory Write）。空的写显存命令不能清屏。旧 `CreateEyePanel()` 还会再次调用 `esp_lcd_panel_disp_on_off(..., true)`。左右面板都完成这一步后，`InitializeEyes()` 才构造 `DualEyeDisplay`，后者才清黑并加载平静动画首帧。因此随机显存已经先被送到显示输出。
- 旧 `InitializePowerOff()` 为避免向断电外设反向供电，将两路 CS 都拉低；屏幕供电后却未先将它们拉高。初始化左屏时只注册了左屏的 SPI IO，右屏 CS 仍为有效低电平，右屏也会收到左屏的初始化命令。直到随后注册右屏 IO，这条非预期广播路径才消失。

背光极性是正确的：GPIO8 低电平关闭 Q4，Q4 门极还有 R56 下拉；旧代码的正常路径也是首帧处理返回后才调用 `RestoreBrightness()` 渐亮。本次分析没有测量背光电气波形，不能据此断言背光提前亮起。代码和手册已确认的是未初始化显存被提前使能输出，以及另一屏片选未及时撤销。

新启动时序如下：

1. 屏幕上电时保持共享 RST 低电平、背光关闭；电源稳定后先将左右 CS 都拉高。
2. 两屏执行一次共享硬件复位；每屏初始化不再重复软件复位，初始化表移除 `0x29` 和空的 `0x2C`，面板始终保持 Display OFF。
3. 关闭显示时依次清黑左右完整显存，分别等待 SPI DMA 完成；加载平静动画并写入左右首帧，再分别等待 DMA 完成。
4. 动画任务创建成功后才标记 `ready`。只有左右屏的 Display ON 都成功，才等待 40 ms，使控制器先扫描有效画面，再恢复背光。
5. 缓冲区或同步对象分配、回调注册、资源校验、解压、像素传输、DMA 等待或任务创建失败时，不进入可开屏状态。任一 Display ON 失败时尝试关闭两屏，背光保持关闭。启动资源错误需要修复资源并重启，重新执行完整的显示初始化。

本地已通过 20 项启动回归测试（1 项正常启动、19 项故障注入），使用实际 `dual_eye_display.cc` 配合主机模拟接口编译运行：覆盖 DMA 缓冲分配、两次同步对象创建、两次回调注册、四次像素写入及各自 DMA 超时、资源缺失或损坏、解压失败、任务创建失败，以及左右 Display ON 失败。测试验证首帧像素、DMA 完成顺序、开屏条件和失败分支，日志保存在 `build/startup_regression.log`。实体屏幕的启动显示效果由下述用户现场观察确认。

构建与实机串口验证：

- ESP-IDF 5.5.4 编译成功，应用为 2,389,968 字节；已通过 COM33 完整烧录，五个写入段的 Hash 校验均通过。
- 启动日志时间为：1138 ms 两屏保持关闭并完成清黑 DMA；1468 ms 左右平静首帧 DMA 完成；1508 ms 两屏已开启并完成 40 ms 等待；1518 ms 调用恢复背光。时间取自 ESP-IDF 启动日志，验证了先完成显存初始化、再开屏、最后恢复背光的顺序。
- 35 秒串口观察期间未出现动画错误或崩溃。烧录及启动日志分别保存在 `build/startup_flash.log` 和 `build/startup_boot.log`。
- 用户已通过长按关机后重新开机现场确认：两块屏幕直接显示正常动画，没有启动马赛克；启动花屏问题已解决。
