# AI 双眼玩偶 V0.2

基于 ESP32-S3R8 的双眼玩偶主板，目标板型为 `ai-dual-eyed-doll`。

## 硬件概览

- 主控：ESP32-S3，16 MB Flash，8 MB Octal PSRAM。
- 显示：两块 160 x 160 GC9D01N 圆形 LCD，共用 SPI 总线，独立片选。
- 音频：ES8311 播放 Codec、ES7210 采集 Codec、MIC1 人声麦克风、MIC3 回声参考。
- 输入：两个 TTP233H 触摸按键、KEY_1、KEY_2 和电源键。
- 网络：支持 Wi-Fi 与 ML307C 4G，通过 KEY_2 长按切换。
- 未使用：IMU、SD 卡；TCA9554 端口仅配置为输入。

## 引脚定义

以 `config.h` 为准，当前主要引脚如下：

| 功能 | 引脚 |
| --- | --- |
| I2C SCL / SDA | GPIO6 / GPIO7 |
| 音频 MCLK / BCLK / WS | GPIO41 / GPIO45 / GPIO46 |
| 音频 DOUT / DIN | GPIO42 / GPIO40 |
| 功放使能 | GPIO16 |
| 电源键 | GPIO2，高电平有效 |
| 屏幕电源 | GPIO15，低有效 |
| 音频电源 | GPIO18，高有效 |
| KEY_1 / KEY_2 | GPIO47 / GPIO48 |
| Touch 1 / Touch 2 | GPIO39 / GPIO38 |
| 屏幕 SPI SCLK / MOSI / DC | GPIO14 / GPIO9 / GPIO12 |
| 屏幕 CS1 / CS2 / RST / 背光 | GPIO10 / GPIO13 / GPIO11 / GPIO8 |
| ML307C TX / RX | GPIO43 / GPIO44 |

GPIO43、GPIO44 专用于 ML307C，不再用作应用串口。日志通过 ESP32-S3 原生 USB Serial/JTAG 查看。

## 音频配置

板级音频实现沿用立创 SZPI 板的 `CustomAudioCodec` 和公共 `BoxAudioCodec`，只替换为本板实际 IO，并使用 GPIO16 控制功放。

- 采样率：输入、输出均为 24 kHz，输入送入 AFE 前重采样为 16 kHz。
- 输入格式：`MR`，MIC1 为人声，MIC3 为扬声器回声参考。
- MIC1 增益：28 dB；MIC3 回声参考增益：0 dB。
- ES7210：选择 TDM 时隙 0、1；DIN 使用 GPIO40。
- AFE：`AFE_TYPE_SR`，面向语音识别。
- AEC：`AEC_MODE_SR_HIGH_PERF`，面向语音识别的高性能回声消除。
- 唤醒：使用 WakeNet 识别“你好小智”。

SR 和 SR AEC 是本板实测可以正常唤醒的配置；`BoxAudioCodec` 保持通用实现，AFE 引擎使用本板已验证的识别配置。

## 按键与触摸

- 电源键短按：切换息屏和亮屏；长按约 3 秒：开机或关机。
- KEY_1 短按：切换 AI 对话状态。
- KEY_1 长按：Wi-Fi 模式下进入或退出配网；4G 模式下不触发配网。
- KEY_2 短按：切换下一种表情，循环显示八种动画。
- KEY_2 长按约 2 秒：切换 Wi-Fi / 4G，保存设置后重启。
- Touch 1：显示开心表情并播放短笑声。
- Touch 2：显示生气表情并播放傲娇语音。

触摸输入为高电平有效，按下时触发，释放时不重复触发。

## 网络配置

设备首次启动时默认使用 ML307C 4G；完成网络选择后，设置保存在 NVS 的 `network` 命名空间中。`type=1` 表示 4G，`type=0` 表示 Wi-Fi。

ML307C 与 ESP32-S3 交叉连接：ESP32-S3 TX GPIO43 接模组 RX，ESP32-S3 RX GPIO44 接模组 TX。通信目标波特率为 921600。DTR 和 PWRKEY 未连接，模组需要独立供电并自行启动。

## 构建与烧录

构建：

```bash
python scripts/build.py ai-dual-eyed-doll
```

烧录应用、分区表、启动配置和资源分区：

```bash
idf.py -p COM9 flash
```

具体串口号按实际设备修改。动画资源位于 `0x800000` 的 `assets` 分区，更新动画后必须重新烧录资源分区；只烧录应用不会更新动画。

若日志出现 `dual_eye.anim is missing from the assets partition`，说明资源分区未更新，需要执行完整烧录。

## 验证状态

- ESP-IDF 5.5.4 构建通过。
- 双屏动画资源已重新打包并烧录，用户已确认双屏正常显示。
- KEY_2 表情循环已完成实机验证。
- 设备已验证可以联网、加载唤醒模型并通过“你好小智”唤醒。
- 开机花屏问题已修复：初始化期间保持屏幕关闭，完成首帧传输后再打开显示和背光。

涉及引脚、电源时序或音频参数时，应优先以 `config.h`、板级源码和启动日志为准。