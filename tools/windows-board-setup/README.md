# Typx ESP32-CAM Windows Setup

Requirements: an ESP32-CAM-MB, a USB data cable, Python 3.11 or newer,
`esptool==4.8.1`, and `pyserial==3.5`.

Install the pinned requirements once:

```powershell
py -3 -m pip install -r requirements.txt
```

The release package includes the prepared merged image at
`dist/typxen-esp32cam-v1.0.0-rc3-merged.bin`. Connect the ESP32-CAM through its
ESP32-CAM-MB board, then run from any downloaded or cloned folder path:

```text
Verify-TypxFirmware.cmd
```

RC3 is the current prerelease. Its core workflow was manually validated on one
original ESP32-CAM; this does not claim broad compatibility. Automatic
simulated-battery decrease remains experimental.

This hardware-free check verifies SHA-256, size, `0xFF` padding, manifest
consistency, embedded offsets/magic/hashes, and the extracted ESP bootloader and
application with the pinned `esptool`. Direct `image_info` on the whole merged
container is not a valid check because offset `0x0` is padding and the
bootloader begins at `0x1000`.

After verification, connect the board and run:

```text
Setup-TypxBoard.cmd
```

The tool resolves the image relative to its own folder. It always asks which
COM port to use, probes it without erasing, requires
an explicit original-ESP32 confirmation, flashes the prepared image, and sends
the hotspot configuration directly over serial. The hotspot password is never
printed or saved.

After setup, the firmware joins the configured hotspot in station mode and
advertises `_typx._tcp`. Typx verifies Protocol `1.0` and schedule format `1`
before using the board. If station mode is unavailable, use the board's AP
recovery fallback. The board reconnects automatically after each typing job.
All discovery and HTTP traffic is local; this is not an internet/cloud API.

Maintainers can reproducibly create the ignored merged image from the current
ESP-IDF `flasher_args.json` offsets with:

```powershell
py -3 build_merged_firmware.py
```
