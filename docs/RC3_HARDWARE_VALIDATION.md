# RC3 Hardware Validation

## Status

Typxen ESP32-CAM firmware **v1.0.0-rc3** is the current recommended release
candidate. It remains a prerelease. RC1 is unsafe for dedicated typing and must
not be used. RC2 is superseded because bonded reconnect could remain stuck
waiting for HID safety release.

Manual validation was completed on 2026-07-14 using one original ESP32-CAM.
This limited result does not establish broad compatibility across ESP32-CAM
variants, keyboards, hosts, or operating-system configurations.

## Passed Results

- Android Bluetooth Identity gear and UI worked.
- A custom 29-character Bluetooth name worked and persisted after reboot.
- Windows removal, re-pairing, ESP32-CAM reconnect, and connected-board status
  worked.
- Standard and Personal dedicated typing worked.
- Prepare Job reached READY without typing automatically.
- Cancel returned the prepared job to IDLE without typing.
- START safety confirmation and the visible eight-second countdown worked.
- Real execution progress was reported.
- Stop during execution reached STOPPED and prevented further characters.
- No modifier remained stuck after Stop.
- Physical reset during execution produced safe interruption recovery.
- The interrupted job did not resume after reset.
- The board reconnected and the keyboard remained normal after reconnect safety
  release.
- Source text and user selections remained preserved.

## Accepted Limitation

Automatic activity-based simulated battery decrease was not observed during
manual testing. Battery configuration, reset, persistence, Battery Service, and
automated tests exist, but automatic decrease remains experimental. It is not
described as manually validated and does not block the validated core typing or
safety-release workflow.
