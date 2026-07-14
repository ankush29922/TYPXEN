# ESP32-CAM Wi-Fi Upload

Without saved phone-hotspot credentials, the ESP32-CAM starts its recovery
WPA2 access point named `Typxen-ESP32CAM-XXXX`. Its 16-character password is
generated once and stored in NVS. Configure station mode over UART with
`wifi-sta-config <ssid> <password>`, then use `wifi-sta-start`. The credentials
survive reboot. `wifi-ap-start` selects recovery AP mode, `wifi-forget` removes
the saved hotspot, `wifi-stop` stops only the ESP32-CAM radio and HTTP server,
and `wifi-info` reports the active mode and IP without printing the hotspot
password.

Station mode remains connected during countdown and HID execution so the phone
can poll status and issue Stop. mDNS is suspended during execution and the HTTP
server runs below the executor task priority. If station connectivity drops
during an active job, the board retries station mode instead of switching to
the recovery AP. Boot/setup station failures may still use the recovery AP.

The Protocol V1 HTTP operations are:

- `GET /v1/info`
- `POST /v1/jobs`
- `PUT /v1/jobs/chunks`
- `POST /v1/jobs/finalize`
- `POST /v1/jobs/start`
- `POST /v1/jobs/stop`
- `POST /v1/jobs/cancel`
- `POST /v1/hid/safety-release`
- `GET /v1/status`
- `GET /v1/result`

Create and finalize requests contain a 16-byte hexadecimal job ID, full size,
and SHA-256. Chunk requests contain sequence, offset, length, and CRC32 query
values with up to 4096 binary body bytes. Data is written directly to the
`typx_stage` raw partition. A schedule becomes READY only after complete SHA-256
and Protocol V1 verification. A READY job is not executed automatically and is
not restored after reboot.

START requires an authenticated BLE HID connection whose reconnect safety
release has completed. A delayed, bounded retry task waits for the Windows HID
report path and sends only all-zero keyboard and consumer reports. Pending or
failed safety returns a stable HTTP 409 and leaves the job READY. The manual
safety-release endpoint can restart the same bounded zero-report check after a
persistent failure. After returning HTTP 202 with `countdownMs: 8000`, the
firmware performs the eight-second countdown and executes the verified schedule
through the existing BLE sink. Wi-Fi stays up. `POST /v1/jobs/stop` works in
COUNTDOWN or EXECUTING and remains STOPPING until all keyboard and consumer keys
are released. `POST /v1/jobs/cancel` works only in READY and emits no HID input.
The UART `stop` command remains available.

Identity updates, battery reset, BLE restart, bond removal, Wi-Fi configuration,
and new uploads are rejected while a job is READY or active. A persistent
active marker prevents reboot from resuming an interrupted job and exposes
`INTERRUPTED_RESET` after boot.

Run the client from the repository root, for example:

```powershell
python tools/typx_upload_client.py info
python tools/typx_upload_client.py upload-standard
python tools/typx_upload_client.py finalize --job-id <ID> --schedule standard
python tools/typx_upload_client.py start --job-id <ID>
python tools/typx_upload_client.py stop
python tools/typx_upload_client.py cancel --job-id <ID>
python tools/typx_upload_client.py result
```
