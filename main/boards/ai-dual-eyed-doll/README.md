# AI 双眼玩偶 V0.2

基于 ESP32-S3R8 的双眼玩偶主板，目标板型为 `ai-dual-eyed-doll`。

## 硬件概览

- 主控：ESP32-S3，32 MB Flash，8 MB Octal PSRAM。
- 显示：两块 160 x 160 GC9D01N 圆形 LCD，共用 SPI 总线，独立片选。
- 音频：ES8311 播放 Codec、ES7210 采集 Codec、MIC1 人声麦克风、MIC3 回声参考。
- 输入：两个 TTP233H 触摸按键、KEY_1、KEY_2 和电源键。
- 网络：支持 Wi-Fi 与 ML307C 4G，通过 KEY_1 单击切换。
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

- 电源键短按：切换 AI 对话状态；长按约 3 秒：开机或关机。
- KEY_1 单击：切换 Wi-Fi / 4G，保存设置后重启。
- KEY_1 连点三次：进入 Wi-Fi 配网；在 4G 模式下先切换到 Wi-Fi 并重启进入配网，保留已有 Wi-Fi 凭据。
- KEY_2 短按：切换下一种表情，循环显示八种动画。
- KEY_1、KEY_2 长按：不绑定功能。
- Touch 1：显示开心表情并播放短笑声。
- Touch 2：显示生气表情并播放傲娇语音。

触摸输入为高电平有效：初始化后等待 2 秒，并检测连续松开约 300 毫秒，才接受新的按下；每次触摸后需再次松开才能触发。触摸表情和音效仅在待机且没有音频播放时执行，连接、聆听、回复和配网期间的触摸直接忽略，不延后播放。

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

具体串口号按实际设备修改。板级配置使用小智 `partitions/v2/32m.csv`：`ota_0`、`ota_1` 各为 4 MB，`assets` 分区从 `0xA00000` 开始、大小为 16 MB。双眼动画资源会作为自定义资源包 `dual_eye_assets.pack` 写入该分区；更新动画后必须重新烧录资源分区，只烧录应用不会更新动画。

从旧 16 MB Flash 更换为 32 MB Flash 后，首次烧录必须执行完整 `idf.py flash`，以同步更新启动配置、分区表、应用和 `assets` 分区。若日志出现 `dual_eye.anim is missing from the assets partition`，说明资源分区未更新，需要执行完整烧录。

## 动画资源打包

资源脚本默认读取 `docs/LCD资源动画/gif`，按 `docs/需求补充说明.md` V4 的顺序打包 14 组：原八组顺序不变，新增 ID 为 `battery`、`network4g`、`network_wifi`、`standby`、`power_on`、`wake`。`wake` 对应 `14_cancel_standby_L/R.gif`。仍采用 v1 格式及 12 字节名称字段，`network_wifi` 恰好占满 12 字节。

从仓库根目录运行（需要 PATH 中已有 Python 3.10+、ffmpeg 和 ffprobe）：

```powershell
python scripts/pack_dual_eye_assets.py --output "$env:TEMP/dual_eye.anim" --package-output main/boards/ai-dual-eyed-doll/assets/dual_eye_assets.pack
```

当输出包已经存在时，脚本自动将其作为基准，逐字节保留 `srmodels.bin`、`index.json` 和其他非动画文件，并校验原八组的左右眼全部 RGB565 帧保持一致；也可以用 `--base-package <原包>` 显式指定基准。请保留仓库中的原包后再重打包。没有基准包时，仅生成动画与最小索引；`--srmodels` 可显式提供或替换模型，但这会更新索引中的模型字段。

脚本检查原始 GIF 的每帧完整尺寸、原始 40ms 延时和无限循环扩展，再用 ffprobe 核验每帧 160×160、连续时间戳、帧数和文档规定总时长，要求左右眼时序相同。ffmpeg 只解码一轮，使用 `-fps_mode passthrough` 禁止自动补帧，之后离线转换为小端 RGB565，再执行 XOR + zlib 帧差压缩及流去重。写入前对全部差流还原并逐帧比较源数据，同时检查资源容器校验和及 16 MiB 容量。

当前生成结果：动画 5,333,517 字节，包含 14 组、2,882 个左右眼帧、17 条唯一差流；完整资源包 5,624,748 字节（约 5.364 MiB），16 MiB 分区剩余 11,152,468 字节。模型仍为 291,042 字节，索引仍为 39 字节。资源验证仅证明离线包内容与源帧一致；新增状态的播放顺序、单轮播放和实机显示需由播放器及应用集成后验证。修改资源后仍须重新烧录资源分区。

## 状态显示规则与接入限制

资源包共 14 组：原八组表情保持原顺序，新增六组 `battery`、`network4g`、`network_wifi`、`standby`、`power_on`、`wake` 用于状态事件。显示优先级为 **battery > boot > network > standby > normal**；普通表情请求受状态显示优先级约束。

- **开机（boot）**：`power_on` 播放一轮，时长 4 秒。
- **网络（network）**：网络尚未就绪时，按当前网络模式持续循环 `network4g` 或 `network_wifi`。退出需同时满足网络已连接、真正完成 OTA 激活（无待处理的 code/challenge），以及协议已启动。WebSocket 音频连接采用按需建立（lazy），不要求音频通道始终打开。掉线或协议错误会返回网络动画，WebSocket 音频连接成功后可恢复就绪状态。
- **低电量（battery）**：复用通用逻辑已有的电量 `<= 0%` 且正在放电（discharging）条件，仅在进入该条件时播放一轮；条件恢复时取消待播或正在播放的提示。本板不另设低电量阈值，也不增加周期提醒。
- **静音待机（standby）**：`SetStandby(true)` 支持先播放一轮静音待机动画，再在 `calm` 和 `sleepy` 间交替，每段随机持续 30–60 秒；`SetStandby(false)` 支持播放一轮 `wake`。项目目前没有真实的静音待机事件，因此该接口尚未接入应用入口；`Idle` 不视为静音待机，也不会自动触发这套序列。

现有按钮与触摸绑定不变。KEY_2 仍只循环原八组表情，不会将新增六组状态动画加入手动轮播。

## 本次验收范围

本次构建检查和 host 测试属于编译及主机侧验证，不等同于烧录后的实机验证。离线资源验证范围见上面的资源打包说明；不能据此宣称新增资源已烧录或 14 组动画已完成实机验收。

本次新增状态仍需烧录对应应用与资源分区后，在实机验收：开机单轮及 4 秒时长、Wi-Fi/4G 未就绪持续循环、激活与协议条件满足后的退出、掉线与协议错误后的返回、WebSocket 音频成功后的恢复、低电量条件进入一次及恢复取消，以及状态抢占优先级。静音待机因缺少真实事件入口，目前只能验证接口行为；实际进入、退出及唤醒流程需接入真实事件后再验收。

## 历史验证状态（本次变更前）

以下为原 README 保留的历史记录，仅描述当时的固件与资源版本，不代表本次新增六组资源已烧录或新增状态逻辑已完成实机验证。

- ESP-IDF 5.5.4 构建通过。
- 双屏动画资源已重新打包并烧录，用户已确认双屏正常显示。
- KEY_2 表情循环已完成实机验证。
- 设备已验证可以联网、加载唤醒模型并通过“你好小智”唤醒。
- 开机花屏问题已修复：初始化期间保持屏幕关闭，完成首帧传输后再打开显示和背光。

涉及引脚、电源时序或音频参数时，应优先以 `config.h`、板级源码和启动日志为准。