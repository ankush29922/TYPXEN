# Android App Installation

## Requirements

- Android 9 or newer.
- A phone whose Bluetooth stack supports Android HID Device mode for phone
  typing. Dedicated ESP32-CAM mode can still be selected separately.
- The `Typx-Offline-License-Polished.apk` asset from the
  [v1.0.0-rc3 release](https://github.com/ankush29922/TYPXEN/releases/tag/v1.0.0-rc3).
- A valid device-bound offline Typx licence.

## Install

1. Download the APK and `SHA256SUMS-v1.0.0-rc3.txt` from GitHub Releases.
2. Verify the APK SHA-256 is
   `2dd5a0f842a19c5ecc0f1dcb3688ebf1d65915cad52728c8104e3aa9b0d38ac9`.
3. Open Android settings for the browser or file manager used to launch the
   APK and temporarily allow installation of unknown apps.
4. Open the APK, review the Android installation prompt, and install Typx.
5. Disable the temporary unknown-app permission if it is no longer needed.
6. Open Typx and activate it with the licence issued for the displayed Device
   Code.

The licence generator and signing authority are private and are not included in
this repository or public release. A licence is checked offline and is bound to
the device code for which it was issued.

## Pair phone mode with Windows

Enable Phone Bluetooth mode in Typx, then use **Settings > Bluetooth & devices >
Add device > Bluetooth** on Windows and select the Typx keyboard advertised by
the phone. Keep Windows on the English (United States) keyboard layout and turn
Caps Lock off.

If the phone does not expose HID Device mode, use a supported phone or the
documented ESP32-CAM dedicated mode. Ordinary Bluetooth availability alone does
not prove HID Device support.
