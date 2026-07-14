# Device Setup

Use **v1.0.0-rc3**, the current release candidate. Its core typing, Stop, and
reset-safety workflow was manually validated on one original ESP32-CAM;
broader board compatibility is not claimed.

## Requirements

- ESP32-CAM with the original ESP32 and 4 MB flash.
- ESP32-CAM-MB programmer and a USB data cable.
- Windows with Python 3.11 or newer.
- `esptool==4.8.1` and `pyserial==3.5` from the bundled requirements file.

Install requirements, connect the board, and run:

```powershell
py -3 -m pip install -r tools\windows-board-setup\requirements.txt
tools\windows-board-setup\Verify-TypxFirmware.cmd
tools\windows-board-setup\Setup-TypxBoard.cmd
```

The verifier never accesses hardware. It understands that the merged file has
`0xFF` padding from `0x0` to the bootloader at `0x1000`; direct `image_info` on
the entire container is therefore expected to reject the first byte.

Select the detected serial port when prompted. The tool verifies that the chip
is an original ESP32 before enabling the flash confirmation. It writes the
bundled merged image at `0x0`, then asks for the phone hotspot name and password
without echoing or storing the password on the computer.

Enable the phone hotspot for daily use. Add the board in the Typxen Android app,
pair `Typxen Keyboard` with Windows, and keep an empty focused editor ready
before starting a typing job. Identity settings are optional and are not part
of first-time setup. Simulated battery configuration, reset, and persistence
are available, but automatic activity-based decrease remains experimental.
