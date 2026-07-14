# Hardware Compatibility

## Supported target

The current supported dedicated target is one **original ESP32-based
ESP32-CAM with 4 MB flash**, programmed through an ESP32-CAM-MB and USB data
cable. RC3 core typing and safety were manually validated on one such board.

The Windows setup tool probes the chip and requires confirmation before flash.
Board names used by sellers are not sufficient evidence; confirm the original
ESP32 SoC and 4 MB flash layout.

## Not currently supported

- XIAO ESP32-S3
- ESP32-S3 boards
- ESP32-WROOM boards
- ESP32-S2 and ESP32-C3 boards
- ESP32-CAM variants with a different SoC or flash layout

These may be discussed only as future work. Do not flash the RC3 merged image to
them and do not report them as supported without a separate implementation and
hardware validation cycle.

## Host assumptions

The validated computer workflow uses Windows, English (United States) keyboard
layout, Caps Lock off, and an empty focused VS Code, LeetCode, or HackerRank
editor. Android support requires Android 9+; phone mode additionally requires a
Bluetooth stack that implements HID Device mode.
