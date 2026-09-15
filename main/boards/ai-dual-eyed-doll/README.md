# AI Dual-Eyed Doll

ESP32-S3R8 dual-eye board: two 0.71" GC9D01N SPI panels, ES8390 codec (ES8389 driver), two TTP233H touch pads, and a power-latch button.

Hardware pinout comes from `docs/原理图与外设手册` (netlist + ESP32-S3 QFN-56 datasheet). This is not pin-compatible with Waveshare ESP32-S3-Touch-LCD-1.85.

## Hardware

- MCU: ESP32-S3R8, 16MB flash, octal PSRAM (GPIO33-37 reserved)
- LCD: 2 x 0.71" GC9D01N 160x160, 4-SPI, shared SCLK/MOSI/DC/RST, separate CS
- Codec: ES8390 on I2C 0x20, driven with the shared ES8389 driver; two differential mics; LM4890 speaker amp
- PA_SD on GPIO18 (KEY_2 removed, R44 DNP)
- Touch_out3 / Touch_out4 are not enabled

Left eye (CS GPIO12) shows the RGB565 asset as packed. Right eye (CS GPIO13) uses `esp_lcd_panel_mirror(x=true)` so both eyes share one compressed bitmap.

Eyes only show the eight zlib-compressed `assets/*.rgb565.z` frames. Fonts, icons, and emoji collections are not compiled or flashed (`BUILTIN_*_FONT` empty, `CONFIG_FLASH_NONE_ASSETS`).

## Controls

| Input | GPIO | Circuit | Action |
| --- | --- | --- | --- |
| Touch 1 | 8 | TTP233H U2, AHLB open, active-high | Happy eyes + `Short_laugh.ogg` |
| Touch 2 | 40 | TTP233H U7, active-high | Angry eyes + `tsundere.ogg` |
| Power | 2 | KEY_ON, 51k pull-up, active-low | Hold 3s to boot. Running: short = screen on/off; hold 3s = power off (`PWR_CTRL` GPIO1) |
| KEY 1 | 17 | 51k pull-up, active-low | Short: toggle AI chat. Long: enter/exit WiFi AP config |
| BOOT | 0 | SW1 download / reset | Unused at runtime |

Touch and power/key events are handled with the shared `Button` state machine. Touch reactions `Schedule()` onto the application task: set emotion, then `PlaySound()` which demuxes OGG into the audio queue. Playback is not blocked inside the GPIO callback. Idle / protocol `SetEmotion("neutral")` returns both eyes to the default cute frame.

## Build

```sh
python scripts/build.py ai-dual-eyed-doll
```
