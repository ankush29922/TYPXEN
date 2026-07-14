# Typx

Typx is Android typing automation with two local Bluetooth HID modes: the phone
can act as the keyboard directly, or an original ESP32-based ESP32-CAM can
receive a verified job over Wi-Fi and execute it offline over Bluetooth. Source
text is emitted as keyboard reports; it is not sent to the computer clipboard.

The current public prerelease is **v1.0.0-rc3**. Download the Android APK,
merged ESP32-CAM firmware, and checksum file from the
[v1.0.0-rc3 GitHub Release](https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3).
Do not download Typx APKs or firmware from third-party mirrors.

## Modes

### Phone Bluetooth HID

On supported Android phones, Typx registers the phone as a Bluetooth HID
keyboard, pairs it with Windows, plans the selected source, shows an eight-second
countdown, and types into the focused editor. Pause, Resume, and Stop are
available in phone mode.

### ESP32-CAM dedicated hardware

The Android app prepares a Protocol V1 schedule and transfers it to the board
over the phone's local Wi-Fi hotspot. The board validates every chunk and the
full SHA-256 digest before entering `READY`. Start follows the explicit flow:

```text
Prepare -> READY -> 8-second countdown -> Execute
```

Once prepared, execution is local to the ESP32-CAM and Windows Bluetooth link.
The phone keeps the local Wi-Fi control path available for status and Stop.
Cancel is available while a job is `READY`; it emits no keyboard input.

## Supported scope

| Area | Current support |
| --- | --- |
| Source languages | C++, Java, Python |
| Editors | VS Code, LeetCode, HackerRank |
| Cadence | Standard and Personal |
| Computer | Windows with English (United States) keyboard layout |
| Android | Android 9+ and HID Device-capable Bluetooth stack |
| Dedicated board | Original ESP32-based ESP32-CAM with 4 MB flash |
| Firmware toolchain | ESP-IDF v5.5.4 |

XIAO ESP32-S3, other ESP32-S3 boards, ESP32-WROOM boards, and other ESP32-CAM
variants are **future work**, not supported targets. No compatibility is claimed
for untested boards, operating systems, custom keyboard layouts, editor
extensions, or remapped shortcuts.

## Install the Android app

1. Download `Typx-Offline-License-Polished.apk` and
   `SHA256SUMS-v1.0.0-rc3.txt` from the release.
2. Verify the APK SHA-256 before installation.
3. On Android, permit installation from the browser or file manager used to
   open the APK, then install it.
4. Open Typx and activate it with a valid device-bound offline licence.

The licence generator is private and is not included in this repository or in
the public release. Typx cannot begin new protected work without a valid offline
licence. Existing Stop and Cancel safety actions remain available.

See [Android app installation](docs/APP_INSTALLATION.md).

## Phone Bluetooth setup

1. On the phone, choose Phone Bluetooth mode and enable the HID service.
2. In Windows Bluetooth settings, add the Typx keyboard shown by the phone.
3. Use Windows English (United States), turn Caps Lock off, and focus an empty
   editor before the countdown ends.
4. Select C++, Java, Python, or automatic detection; choose VS Code, LeetCode,
   or HackerRank; then use Standard or Personal cadence.

Source indentation must use spaces. Tab characters and unsupported US-English
HID characters are blocked before execution. Typx assumes the source is already
complete and syntactically correct.

## ESP32-CAM setup and flashing

Use an ESP32-CAM-MB programmer, a USB data cable, Windows, and Python 3.11+.
Download `typxen-esp32cam-v1.0.0-rc3-merged.bin` from the release and place it
in `dist/` if it is not already present in the cloned package.

```powershell
py -3 -m pip install -r tools\windows-board-setup\requirements.txt
tools\windows-board-setup\Verify-TypxFirmware.cmd
tools\windows-board-setup\Setup-TypxBoard.cmd
```

The setup tool discovers serial ports and does not assume `COM6`. It verifies
an original ESP32, asks before flashing, writes the merged image at address
`0x0`, and sends hotspot credentials directly over serial without saving or
printing the password.

For manual flashing:

```powershell
py -3 -m esptool --chip esp32 --port <COM_PORT> --before default_reset --after hard_reset write_flash --flash_size 4MB 0x0 dist\typxen-esp32cam-v1.0.0-rc3-merged.bin
```

After flashing, enable the configured phone hotspot, add the discovered board
in Typx, and pair `Typxen Keyboard` with Windows. The phone uploads a verified
job over Wi-Fi; the board then performs the prepared job through BLE HID.

See [firmware flashing](docs/FIRMWARE_FLASHING.md),
[device setup](docs/DEVICE_SETUP.md), and [hardware compatibility](docs/HARDWARE_COMPATIBILITY.md).

## Execution safety

- A job cannot start until upload validation, Bluetooth authentication, and
  reconnect modifier-release safety are complete.
- Stop is accepted during countdown or execution and does not report `STOPPED`
  until keyboard and consumer reports have been released.
- Cancel removes a prepared `READY` job without emitting HID input.
- RC3 records interrupted execution and never resumes it automatically after a
  reset. After authenticated reconnect it sends only all-zero keyboard and
  consumer reports before allowing another run.
- If a modifier appears stuck after power loss, power off the board, toggle
  Windows Bluetooth off, wait five seconds, and turn Bluetooth on again.

Full recovery guidance is in [Recovery](docs/RECOVERY.md).

## Current limitations

- RC3 core typing and safety were manually validated on one original ESP32-CAM;
  this is not broad hardware certification.
- The target computer must use Windows English (United States), Caps Lock off,
  an empty focused editor, spaces for indentation, and supported ASCII HID
  characters.
- Typx does not compile or syntax-check source.
- Editor auto-completion, extensions, custom keymaps, and web-editor updates can
  change results.
- The ESP32-CAM battery value is simulated; automatic activity-based decrease
  remains experimental and is not a physical charge measurement.
- Offline licences cannot be remotely revoked.

## Verify downloads

Expected release hashes:

```text
2dd5a0f842a19c5ecc0f1dcb3688ebf1d65915cad52728c8104e3aa9b0d38ac9  Typx-Offline-License-Polished.apk
98176a6006435f9de9f35c52a7a39e58c63d194c690173fc6d8540d60bcbde93  typxen-esp32cam-v1.0.0-rc3-merged.bin
```

On Windows:

```powershell
Get-FileHash -Algorithm SHA256 .\Typx-Offline-License-Polished.apk
Get-FileHash -Algorithm SHA256 .\typxen-esp32cam-v1.0.0-rc3-merged.bin
```

## Source, tests, and protocol

Build firmware with ESP-IDF v5.5.4:

```powershell
cd targets\esp32_cam
idf.py build
```

Host-side tests cover Protocol V1, Wi-Fi upload, local execution, HID runtime,
discovery, identity, Windows setup, and release-package privacy checks. Start
with [Documentation index](docs/INDEX.md), [Protocol overview](docs/PROTOCOL.md),
or [Troubleshooting](docs/TROUBLESHOOTING.md).

## Licence and reuse

No open-source or other reuse licence has been selected. No permission to copy,
modify, redistribute, or reuse the source is currently granted beyond rights
provided by applicable law. The public availability of this repository does not
itself grant a reuse licence.
