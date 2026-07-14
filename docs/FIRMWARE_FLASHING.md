# Firmware Flashing

## Before flashing

Use only the original ESP32-based ESP32-CAM with 4 MB flash. Download the RC3
merged image and checksum file from the
[GitHub Release](https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3).
The firmware SHA-256 must be
`98176a6006435f9de9f35c52a7a39e58c63d194c690173fc6d8540d60bcbde93`.

Requirements are Windows, Python 3.11+, an ESP32-CAM-MB, a USB data cable,
`esptool==4.8.1`, and `pyserial==3.5`.

## Guided setup

Place the downloaded binary at
`dist\typxen-esp32cam-v1.0.0-rc3-merged.bin`, then run:

```powershell
py -3 -m pip install -r tools\windows-board-setup\requirements.txt
tools\windows-board-setup\Verify-TypxFirmware.cmd
tools\windows-board-setup\Setup-TypxBoard.cmd
```

The tool lists detected serial ports instead of assuming a fixed port. It
probes the chip, asks for confirmation, flashes at `0x0`, and configures the
phone hotspot over serial without storing the password on the PC.

## Manual flash

```powershell
py -3 -m esptool --chip esp32 --port <COM_PORT> --before default_reset --after hard_reset write_flash --flash_size 4MB 0x0 dist\typxen-esp32cam-v1.0.0-rc3-merged.bin
```

The merged container begins with padding and places the bootloader at `0x1000`.
Use the bundled verifier rather than running `esptool image_info` against the
whole merged file.

After flashing, enable the configured phone hotspot, wait for the board to join,
add the board in Typx, and pair `Typxen Keyboard` in Windows Bluetooth settings.
See [Recovery](RECOVERY.md) before erasing flash.
