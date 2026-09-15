# AI Dual-Eyed Doll

AI 双眼玩偶开发板。

硬件基于 微雪（Waveshare）ESP32-S3-Touch-LCD-1.85，本目录由 `main/boards/waveshare/esp32-s3-touch-lcd-1.85` 复制而来，作为独立 board 维护，后续的定制改动均在此目录进行。

参考产品链接：
https://www.waveshare.net/shop/ESP32-S3-Touch-LCD-1.85.htm

## 选用方式

```bash
idf.py set-target esp32s3
idf.py menuconfig   # Xiaozhi Assistant -> Target Board -> AI Dual-Eyed Doll
idf.py build
```

对应的 Kconfig 选项为 `CONFIG_BOARD_TYPE_AI_DUAL_EYED_DOLL`，CMake 中的 `BOARD_DIR` 为 `ai-dual-eyed-doll`。
