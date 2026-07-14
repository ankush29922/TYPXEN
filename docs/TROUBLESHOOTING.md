# Troubleshooting

## Setup tool cannot find a board

Use a USB data cable, reseat the ESP32-CAM in the ESP32-CAM-MB, close serial
monitors, and retry. Select the port that appears when the programmer is
connected. The tool does not assume a fixed port.

## Board is offline in Typxen

Confirm the phone hotspot is enabled with the saved name/password and that the
phone is connected to the same local network. Reopen the saved board after the
firmware reconnects. Use the recovery AP or serial setup to replace changed
hotspot credentials.

## Windows shows the old keyboard name

Remove the previous keyboard under Windows Bluetooth & devices, restart from
the app if still required, then search and pair again. Identity-triggered
restart clears the board's old BLE bonds; ordinary reboot does not.

## Typing does not start

BLE must already be connected, the schedule must finalize successfully, and no
other execution may be active. The app also waits for authoritative HID safety
readiness. A temporary waiting state may resolve after authenticated reconnect;
use the app's bounded safety-release retry when it reports a persistent failure.
Inspect `/v1/status` or `/v1/result` through the app. Never execute a partially
uploaded or checksum-failed schedule.

## Control connection is lost during typing

The board may still be typing. Do not assume Stop succeeded until status says
`STOPPED`. Keep the phone hotspot enabled and let the app retry. If the keyboard
is stuck after power loss, follow the [stuck keyboard recovery](RECOVERY.md)
steps. Do not repeatedly reset the board during typing.

## Simulated battery does not decrease

Automatic activity-based decrease was not observed during RC3 manual testing
and remains experimental. Configuration, reset, persistence, Battery Service,
and automated coverage are present, but this behavior is not part of the
manually validated core typing and safety result. Use battery controls for
testing only; the value is not a physical charge measurement.
