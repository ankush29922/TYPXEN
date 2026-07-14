# Local HTTP API

The firmware serves a local API over its active Wi-Fi network. `GET /v1/info`
reports protocol `1.0`, schedule format `1`, limits, firmware information, and
capabilities including `deviceIdentityConfig`, `batterySimulationConfig`,
`bleRestart`, and `dedicatedRunControl`.

Schedule operations are:

- `POST /v1/jobs`
- `PUT /v1/jobs/chunks`
- `POST /v1/jobs/finalize`
- `POST /v1/jobs/start`
- `POST /v1/jobs/stop`
- `POST /v1/jobs/cancel`
- `POST /v1/hid/safety-release`
- `GET /v1/status`
- `GET /v1/result`

Uploads use 4 KiB chunks with job ID, offset, length, and CRC32. Finalization
checks full size and SHA-256. START is accepted only for a fully verified READY
job and a connected, safety-released BLE keyboard host. The successful response
is HTTP 202 with `state: COUNTDOWN` and `countdownMs: 8000`.
While the release is incomplete, START returns HTTP 409 with
`HID_NOT_CONNECTED`, `HID_SAFETY_PENDING`, or `HID_SAFETY_FAILED` and leaves
the job READY. The optional safety-release endpoint retries only all-zero
keyboard and consumer reports while no job is active.

Wi-Fi remains connected and the low-priority HTTP status/control path remains
available during COUNTDOWN and EXECUTING. Stop is accepted in either state,
sets an immediate thread-safe request, and is idempotent through STOPPING and
STOPPED. It never reports STOPPED until release processing is complete. Cancel
is accepted only in READY, removes the prepared job, and emits no HID report.

`GET /v1/status` exposes the authoritative state, completed and total records,
current source index, BLE connectivity, authentication, safety state, attempt
count, stable safety error, retry availability, and `startAllowed`. A boot that
finds an active execution marker returns `INTERRUPTED_RESET`; it never resumes
the prior job.

See [Wi-Fi upload](WIFI_UPLOAD.md) for lifecycle and safety details and
[Bluetooth Identity API](BLUETOOTH_IDENTITY_API.md) for identity endpoints.
