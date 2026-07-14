# Bluetooth Identity API

The identity API is local to the board's existing HTTP service. It does not
read, return, or modify phone-hotspot credentials.

## Endpoints

- `GET /v1/device-identity` returns effective identity, simulated battery,
  BLE connection, restart requirement, board identifier, and value sources.
- `PUT /v1/device-identity` validates and atomically accepts one complete
  identity request. `firmwareRevision` is read-only and is rejected in input.
- `POST /v1/device-identity/reset` removes only `ble_identity` overrides.
- `POST /v1/device-identity/battery-reset` restores the configured maximum and
  clears accumulated HID activity.
- `POST /v1/ble/restart` returns `202`, releases keys, conditionally clears old
  BLE bonds for an identity change, and performs a controlled board reboot.

Text is printable US-ASCII without leading/trailing whitespace. Limits are 29
characters for the Bluetooth name, 32 for manufacturer/model/serial, and 16
for revisions. Serial may be empty; the board then derives a stable privacy-
reduced `TYPX-XXXXXXXX` value from chip identity.

Battery minimum is 0-99, maximum is 1-100, minimum must be lower, current must
be in range, and drop interval is 1-1440 active minutes. Server validation is
authoritative and invalid requests do not change the effective configuration.

Battery configuration, reset, persistence, and automated tests are present.
Automatic activity-based decrease was not observed during RC3 manual testing
and remains experimental; it must not be treated as a measured battery value or
as manually validated release behavior.

Identity strings are cosmetic. Numeric PnP identifiers remain neutral and are
not user-configurable. After an identity restart, remove the previous Windows
Bluetooth pairing and pair again with the configured name.

All identity writes, identity/default reset, battery reset, and BLE restart
return HTTP 409 with `DEDICATED_JOB_ACTIVE` while a dedicated job is READY,
COUNTDOWN, EXECUTING, or STOPPING. This prevents configuration and bond changes
from interrupting a prepared or active schedule.
