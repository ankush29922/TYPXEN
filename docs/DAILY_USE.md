# Daily Use

1. Enable the configured phone hotspot.
2. Power the ESP32-CAM and wait for it to join the hotspot.
3. Confirm the Typxen BLE keyboard is paired and connected to Windows.
4. Open Typxen on Android and select the saved ESP32-CAM board.
5. Prepare an empty, focused editor using Windows English (United States).
6. Tap `Prepare job` and wait for `Job loaded on ESP32-CAM`.
7. Review the prepared job, focus the empty editor, and tap `Start typing`.

The board acknowledges START before the app begins the authoritative
eight-second countdown. Wi-Fi and the minimal HTTP control/status path remain
available during countdown and execution. The run screen reports actual record
progress and provides Stop. A normal stop releases keyboard and consumer keys
and remains `STOPPING` until the board confirms `STOPPED`.

Do not reset the ESP32-CAM during typing. If power is lost or the board is
reset, the next boot reports `INTERRUPTED_RESET` and sends only all-zero
keyboard and consumer reports after the authenticated BLE reconnect. Windows
may negotiate its own Bluetooth supervision timeout, so use the run screen's
`Keyboard stuck?` recovery instructions if a modifier remains held.

Bluetooth Identity settings are optional. Saving only battery values does not
require re-pairing. Saving identity/service values requires a controlled reboot:
remove the previous Windows pairing, search again, and pair using the new name.
The displayed battery is simulated from successful HID activity, not measured
electrical charge. Automatic activity-based decrease was not observed during
RC3 manual testing and remains experimental; battery configuration, reset, and
persistence do not affect the validated typing or safety workflow.

RC3's core workflow was manually validated on one original ESP32-CAM. This is
appropriate for the current limited release-candidate scope and does not claim
broad compatibility across board variants or host systems.
