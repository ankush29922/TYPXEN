# Recovery And Reflash

Normal reflashing does not require an erase:

```powershell
tools\windows-board-setup\Verify-TypxFirmware.cmd
py -3 -m esptool --chip esp32 --port <COM_PORT> --before default_reset --after hard_reset write_flash --flash_size 4MB 0x0 dist\typxen-esp32cam-v1.0.0-rc3-merged.bin
```

Do not use direct `esptool image_info` on the complete merged container. Its
first `0x1000` bytes are flash padding, so the whole-file command sees `0xFF`
instead of ESP image magic `0xE9`. Use the bundled verifier, which checks and
extracts the embedded ESP images safely.

Use the bundled Windows setup tool when hotspot credentials must also be
configured. It probes the target and requires confirmation before flashing.

Full erase removes all NVS data, including Wi-Fi credentials, generated recovery
AP settings, BLE bonds, identity overrides, and staged job metadata. Use it only
for deliberate factory recovery:

```powershell
py -3 -m esptool --chip esp32 --port <COM_PORT> erase_flash
```

After an erase, flash the merged image again at `0x0` and rerun setup. Do not
erase solely to change Bluetooth identity; use the Android identity screen.

## Stuck Keyboard Recovery

A physical reset cannot release a key before power disappears. RC3 records an
active run before countdown and, after reboot, never resumes it. Once the BLE
link reconnects and authentication is ready, the firmware sends all-zero
keyboard and consumer reports with bounded retries before another job can run.

If Windows still treats a modifier as pressed:

1. Power off the ESP32-CAM.
2. Turn Windows Bluetooth off.
3. Wait five seconds.
4. Turn Windows Bluetooth on.
5. Do not repeatedly press the ESP32 reset button during typing.

The firmware requests a 3000 ms supervision timeout, but Windows may negotiate
or override that value. Recovery speed therefore cannot be guaranteed while
the board has no power or connection.
