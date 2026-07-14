# Typx v1.0.0-rc3

**Prerelease - core workflow manually validated on one original ESP32-CAM**

## Assets

- `Typx-Offline-License-Polished.apk` - Android app for Android 9+.
- `typxen-esp32cam-v1.0.0-rc3-merged.bin` - merged firmware for the
  original ESP32-based ESP32-CAM with 4 MB flash.
- `SHA256SUMS-v1.0.0-rc3.txt` - SHA-256 values for both assets.

The private Licence Maker administrator APK and signing authority are not part
of this release.

## Install the Android APK

1. Download the APK and checksum file from this release.
2. Verify the APK SHA-256 shown below.
3. Allow installation from the Android browser or file manager used to open the
   APK, install it, then disable that temporary permission if no longer needed.
4. Open Typx and activate it with a valid device-bound offline licence.

Phone Bluetooth HID mode also requires a phone Bluetooth stack that supports
Android HID Device mode. Pair the Typx keyboard from Windows Bluetooth settings.

## Flash the ESP32-CAM

Use an ESP32-CAM-MB, USB data cable, Windows, Python 3.11+, `esptool==4.8.1`,
and `pyserial==3.5`. The supported board is only the original ESP32-based
ESP32-CAM with 4 MB flash.

The merged image flash address is **`0x0`**:

```powershell
py -3 -m esptool --chip esp32 --port <COM_PORT> --before default_reset --after hard_reset write_flash --flash_size 4MB 0x0 typxen-esp32cam-v1.0.0-rc3-merged.bin
```

The repository's Windows setup tool discovers ports, verifies an original
ESP32, asks before flashing, and configures the phone hotspot over serial
without storing the password on the PC.

## Pair and run

1. Enable the configured phone hotspot and wait for the ESP32-CAM to join.
2. Add the board in Typx and pair `Typxen Keyboard` with Windows.
3. Use Windows English (United States), Caps Lock off, and an empty focused
   VS Code, LeetCode, or HackerRank editor.
4. Choose C++, Java, or Python and Standard or Personal cadence.
5. Use **Prepare**, wait for **READY**, start the authoritative **eight-second
   countdown**, focus the editor, and let the board **Execute**.
6. Stop remains available during countdown and execution. Cancel safely removes
   a READY job without typing.

## RC3 safety improvements

RC3 waits for authenticated BLE reconnect before attempting a generation-bound
modifier release, retries with scheduler-friendly delays, sends only all-zero
keyboard and consumer reports during recovery, and permits Start only after both
release reports succeed. Status exposes readiness and stable errors, and a
bounded manual safety-release endpoint is available while no job is active.

An interrupted board reset never resumes the old job. The board reports the
interruption and performs the zero-report recovery after authenticated
reconnect.

## Known limitations

- Manual core validation covers one original ESP32-CAM, not every board or host.
- XIAO ESP32-S3, ESP32-S3, ESP32-WROOM, and other board variants are future
  work and are not supported by this image.
- Android phone mode requires HID Device support.
- Windows English (United States), Caps Lock off, spaces for indentation,
  supported ASCII HID input, and an empty focused editor are required.
- Typx does not compile or syntax-check source.
- Editor extensions, custom keymaps, and auto-completion can affect results.
- Simulated ESP32-CAM battery decrease remains experimental and is not a power
  measurement.
- A valid device-bound offline licence is required for new protected work. The
  private licence generator is not included, and offline licences cannot be
  remotely revoked.

## SHA-256

```text
2dd5a0f842a19c5ecc0f1dcb3688ebf1d65915cad52728c8104e3aa9b0d38ac9  Typx-Offline-License-Polished.apk
98176a6006435f9de9f35c52a7a39e58c63d194c690173fc6d8540d60bcbde93  typxen-esp32cam-v1.0.0-rc3-merged.bin
```
