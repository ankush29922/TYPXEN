<p align="center">
  <img src="assets/typxen-logo.png" alt="Typx app icon" width="160">
</p>

<h1 align="center">TYPXEN</h1>

<p align="center">
  Local Android typing automation with phone and ESP32-CAM Bluetooth HID execution.
</p>

<p align="center">
  <a href="https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3"><img src="https://img.shields.io/github/v/release/ankush29922/TYPXEN?include_prereleases&amp;style=flat-square&amp;label=release" alt="GitHub release"></a>
  <img src="https://img.shields.io/badge/Android-9%2B-3DDC84?style=flat-square" alt="Android 9 or newer">
  <img src="https://img.shields.io/badge/target-original%20ESP32--CAM-E7352C?style=flat-square" alt="Original ESP32-CAM target">
</p>

<p align="center">
  <strong><a href="https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3">Latest Release</a></strong>
  · <strong><a href="https://github.com/ankush29922/TYPXEN/releases/download/v1.0.0-rc3/Typx-Offline-License-Polished.apk">Android APK</a></strong>
  · <strong><a href="https://github.com/ankush29922/TYPXEN/releases/download/v1.0.0-rc3/typxen-esp32cam-v1.0.0-rc3-merged.bin">ESP32-CAM Firmware</a></strong>
  · <strong><a href="docs/DEVICE_SETUP.md">Setup Guide</a></strong>
</p>

<p align="center">
  <a href="#overview">Overview</a> ·
  <a href="#mode-comparison">Modes</a> ·
  <a href="#supported-languages-and-editors">Compatibility</a> ·
  <a href="#how-it-works">How it works</a> ·
  <a href="#quick-start">Quick start</a> ·
  <a href="#download-and-checksum-verification">Downloads</a> ·
  <a href="#documentation-index">Documentation</a> ·
  <a href="#security">Security</a>
</p>

## Overview

Typx turns prepared C++, Java, or Python source into Bluetooth keyboard input
for a focused Windows editor. It supports direct Phone Bluetooth typing and a
dedicated ESP32-CAM mode with local Wi-Fi job transfer and offline BLE HID
execution.

The current firmware is **v1.0.0-rc3**. The dedicated target is the **original
ESP32-based ESP32-CAM with 4 MB flash**.

## What Typx does

Typx accepts selected or entered source, validates it against the supported
US-English HID mapping, plans editor-safe key actions, and types the result into
VS Code, LeetCode, or HackerRank. Source is sent as keyboard reports, not through
the laptop clipboard.

## Key features

- Phone Bluetooth HID and dedicated ESP32-CAM execution modes.
- Standard and Personal cadence modes.
- C++, Java, and Python source profiles.
- Tested editor profiles for VS Code, LeetCode, and HackerRank.
- Visible eight-second countdown before typing begins.
- Stop, Cancel, progress reporting, and modifier-release safety.
- Local operation without a cloud account or typing service.

## Mode comparison

| | Phone Bluetooth | Dedicated ESP32-CAM |
| --- | --- | --- |
| HID sender | Android phone | ESP32-CAM |
| Job transfer | Not required | Local Wi-Fi upload |
| Execution link | Phone to laptop over Bluetooth | Board to laptop over Bluetooth |
| Cadence | Standard or Personal | Standard or Personal |
| Controls | Pause, Resume, Stop | Prepare, Start, Stop, Cancel |
| Best suited for | Direct phone-based use | Dedicated offline board execution |

## Phone Bluetooth mode

The Android phone registers as a Bluetooth HID keyboard and pairs directly with
Windows. Typx plans the source on the phone, displays the countdown, and types
into the focused editor. Pause, Resume, and Stop are available during a phone
typing session.

Phone mode requires Android 9 or newer and a Bluetooth stack that supports HID
Device mode.

## Dedicated ESP32-CAM mode

The Android app prepares a Protocol V1 schedule and transfers it to the board
over the phone's local Wi-Fi hotspot. The board validates the transfer and
payload digest before it can start.

```text
Prepare -> READY -> 8-second countdown -> Execute
```

Execution then runs locally from the ESP32-CAM over Bluetooth HID. Stop remains
available during countdown and execution. Cancel safely removes a prepared
`READY` job without sending keyboard input.

## Supported languages and editors

| Category | Tested support |
| --- | --- |
| Languages | C++, Java, Python |
| Editors | VS Code, LeetCode, HackerRank |
| Cadence | Standard, Personal |
| Computer layout | Windows English (United States) |

Typx assumes source is complete and syntactically correct. It does not compile
or syntax-check code.

## Supported hardware

The current dedicated target is the **original ESP32-based ESP32-CAM with 4 MB
flash**, typically programmed through an ESP32-CAM-MB and a USB data cable.

XIAO ESP32-S3, other ESP32-S3 boards, ESP32-WROOM boards, and other ESP32-CAM
variants are future work and are not supported by the RC3 image. See
[Hardware compatibility](docs/HARDWARE_COMPATIBILITY.md).

## How it works

```mermaid
flowchart LR
    A[Android phone] -->|Wi-Fi job upload| B[Original ESP32-CAM]
    B -->|Bluetooth HID| C[Windows laptop]
```

In dedicated mode, Android remains responsible for source preparation, editor
planning, cadence resolution, and schedule creation. The ESP32-CAM receives
only finalized HID records, verifies them, and executes them after the
authoritative countdown.

## Quick start

1. Download the APK and checksum file from the
   [v1.0.0-rc3 release](https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3).
2. Verify the downloaded SHA-256 value.
3. Install Typx on Android and activate its device-bound offline licence.
4. Pair the phone directly with Windows, or flash and pair the supported
   ESP32-CAM for dedicated mode.
5. Use Windows English (United States), turn Caps Lock off, and focus an empty
   supported editor before the countdown finishes.

### Android installation

Download
[`Typx-Offline-License-Polished.apk`](https://github.com/ankush29922/TYPXEN/releases/download/v1.0.0-rc3/Typx-Offline-License-Polished.apk),
verify its checksum, and allow installation from the Android browser or file
manager used to open it. Android 9 or newer is required. Follow the
[Android installation guide](docs/APP_INSTALLATION.md) for the complete steps.

### ESP32-CAM setup

Download the
[`v1.0.0-rc3` merged firmware](https://github.com/ankush29922/TYPXEN/releases/download/v1.0.0-rc3/typxen-esp32cam-v1.0.0-rc3-merged.bin)
and place it in `dist/`. On Windows with Python 3.11 or newer:

```powershell
py -3 -m pip install -r tools\windows-board-setup\requirements.txt
tools\windows-board-setup\Verify-TypxFirmware.cmd
tools\windows-board-setup\Setup-TypxBoard.cmd
```

The setup tool discovers available serial ports, verifies an original ESP32,
asks before flashing, and writes the merged image at address `0x0`. See
[Firmware flashing](docs/FIRMWARE_FLASHING.md) and
[Device setup](docs/DEVICE_SETUP.md).

## Download and checksum verification

All public downloads are attached to the
[v1.0.0-rc3 GitHub Release](https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3).

| Asset | SHA-256 |
| --- | --- |
| `Typx-Offline-License-Polished.apk` | `2dd5a0f842a19c5ecc0f1dcb3688ebf1d65915cad52728c8104e3aa9b0d38ac9` |
| `typxen-esp32cam-v1.0.0-rc3-merged.bin` | `98176a6006435f9de9f35c52a7a39e58c63d194c690173fc6d8540d60bcbde93` |

The same values are available in
[`SHA256SUMS-v1.0.0-rc3.txt`](SHA256SUMS-v1.0.0-rc3.txt).

```powershell
Get-FileHash -Algorithm SHA256 .\Typx-Offline-License-Polished.apk
Get-FileHash -Algorithm SHA256 .\typxen-esp32cam-v1.0.0-rc3-merged.bin
```

## Offline licence requirement

The Android app requires a valid, device-bound offline licence before starting
new protected work. The **private License Maker and signing authority are not
included** in this repository or its public release. Offline licences cannot be
remotely revoked.

## Safety and recovery

- Dedicated Start remains blocked until the job, BLE authentication, and
  reconnect modifier-release checks are ready.
- Stop is supported during countdown and execution.
- Cancel removes a prepared `READY` job without typing.
- RC3 does not resume an interrupted job after reset.
- Recovery sends only all-zero keyboard and consumer reports before another run
  can start.

If keyboard state appears stuck after power loss, stop the session and follow
the [Recovery guide](docs/RECOVERY.md) before starting another job.

## Documentation index

- [Android installation](docs/APP_INSTALLATION.md)
- [Device setup](docs/DEVICE_SETUP.md)
- [Firmware flashing](docs/FIRMWARE_FLASHING.md)
- [Hardware compatibility](docs/HARDWARE_COMPATIBILITY.md)
- [Daily use](docs/DAILY_USE.md)
- [Protocol overview](docs/PROTOCOL.md)
- [Local HTTP API](docs/HTTP_API.md)
- [Recovery](docs/RECOVERY.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [RC3 hardware validation](docs/RC3_HARDWARE_VALIDATION.md)
- [Complete documentation index](docs/INDEX.md)
- [Changelog](CHANGELOG.md)
- [Contributing](CONTRIBUTING.md)

## Known limitations

- RC3 core typing and safety were manually validated on one original
  ESP32-CAM; this is not broad hardware certification.
- Windows English (United States), Caps Lock off, spaces for indentation, and
  an empty focused editor are required.
- Unsupported characters, Tab indentation, custom keymaps, extensions, and
  editor auto-completion can affect or block execution.
- The simulated ESP32-CAM battery value is not a physical charge measurement;
  automatic activity-based decrease remains experimental.
- Offline licences cannot be remotely revoked.

## Troubleshooting

For board discovery, Bluetooth re-pairing, upload, Start, Stop, or stuck-key
issues, use the [Troubleshooting guide](docs/TROUBLESHOOTING.md). Do not assume
a Stop request succeeded until the board reports `STOPPED`.

## Security

Verify release hashes before installing or flashing. Do not publish device
codes, licence tokens, credentials, captured source, network identifiers, or
private logs in an issue. Follow the repository's [Security Policy](SECURITY.md)
for responsible reporting.

## Licence and reuse notice

No open-source or other reuse licence has been selected. No permission to copy,
modify, redistribute, or reuse the source is currently granted beyond rights
provided by applicable law. Public availability of this repository does not by
itself grant a reuse licence.
