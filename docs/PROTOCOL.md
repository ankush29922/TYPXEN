# Protocol Overview

Typx plans source on Android and sends the dedicated board only finalized HID
records. Source text, Personal cadence profiles, licence tokens, and Android
device identifiers are not part of the board schedule.

Protocol V1 uses 4 KiB Wi-Fi upload chunks with CRC32 and validates the complete
payload with SHA-256. A job becomes `READY` only after format, limits, timing,
record flags, keycodes, modifiers, cleanup records, and digest all pass.

Start is accepted only when the current verified job is `READY`, BLE is
connected and authenticated, and reconnect safety has released keyboard and
consumer modifiers. The successful response returns the authoritative
eight-second countdown. Stop remains locally reachable during countdown and
execution; Cancel is restricted to `READY`.

Detailed contracts:

- [Schedule Protocol V1](protocol/ESP32_CAM_PROTOCOL_V1.md)
- [State machine](protocol/ESP32_CAM_STATE_MACHINE.md)
- [Firmware release contract](protocol/FIRMWARE_RELEASE_CONTRACT.md)
- [Local HTTP API](HTTP_API.md)
- [Wi-Fi upload lifecycle](WIFI_UPLOAD.md)
