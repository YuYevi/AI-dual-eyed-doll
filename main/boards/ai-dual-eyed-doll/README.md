# AI Dual-Eyed Doll

AI 双眼玩偶开发板，基于 ESP32-S3R8。

## 硬件

- 两块 0.71 英寸、160×160、4 线 SPI 的 GC9D01 彩色屏
- ES8390 音频 Codec（复用 ES8389 驱动），两个模拟麦克风和一个扬声器
- KEY_ON 电源按键、KEY_1 对话按键
- 两路 TTP233H 电容触摸
- 4MB Flash 和片内 Octal PSRAM

GPIO33～GPIO37 由 PSRAM 占用，不用于普通外设。Touch_out3、Touch_out4 和 KEY_2 均不启用；硬件改板后 PA_SD 使用 GPIO18，R44 已去除。

## 行为

- 两块屏共用同一份压缩 RGB565 表情资源，右眼由 GC9D01 水平镜像显示。
- 电源键短按切换息屏/亮屏，长按 3 秒开机或关机。
- KEY_1 短按切换 AI 对话状态，长按进入或退出配网。
- Touch 1 显示开心表情并播放 `Short_laugh.ogg`。
- Touch 2 显示生气表情并播放 `tsundere.ogg`。
- 当前 Board 不使用字体、图标或 emoji 资源。

## 编译

```bash
python scripts/build.py ai-dual-eyed-doll
```

对应 Kconfig 选项为 `CONFIG_BOARD_TYPE_AI_DUAL_EYED_DOLL`，CMake 中的 `BOARD_DIR` 为 `ai-dual-eyed-doll`。