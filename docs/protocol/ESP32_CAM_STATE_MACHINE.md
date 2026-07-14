# ESP32-CAM State And Transfer Contract

This document describes the RC3 firmware and Android dedicated-run contract.

## States And Legal Transitions

| State | Legal next states |
| --- | --- |
| `IDLE` | `UPLOADING`, `INTERRUPTED_RESET` |
| `UPLOADING` | `VERIFYING`, `IDLE`, `FAILED` |
| `VERIFYING` | `READY`, `FAILED` |
| `READY` | `COUNTDOWN`, `IDLE`, `FAILED` |
| `COUNTDOWN` | `EXECUTING`, `STOPPING`, `FAILED` |
| `EXECUTING` | `COMPLETED`, `STOPPING`, `FAILED` |
| `STOPPING` | `STOPPED`, `FAILED` |
| `STOPPED` | `UPLOADING` |
| `COMPLETED` | `UPLOADING` |
| `FAILED` | `UPLOADING` |
| `INTERRUPTED_RESET` | `UPLOADING` |

`READY -> COUNTDOWN` is legal only for a fully verified current job while HID
is connected, authenticated, and reconnect safety releases are complete. START
returns HTTP 202 with `countdownMs: 8000` before the countdown task is notified.
Wi-Fi and low-priority HTTP status/control remain available throughout.
Execution never resumes automatically after reboot.

Stable planned terminal/control errors are:

- `PROTOCOL_MAJOR_INCOMPATIBLE`
- `SCHEDULE_FORMAT_INCOMPATIBLE`
- `HEADER_INVALID`
- `SCHEDULE_LIMIT_EXCEEDED`
- `CHUNK_CRC_MISMATCH`
- `CHUNK_CONFLICT`
- `PAYLOAD_SHA256_MISMATCH`
- `JOB_NOT_VERIFIED`
- `HID_NOT_CONNECTED`
- `HID_SAFETY_PENDING`
- `HID_SAFETY_FAILED`
- `INVALID_STATE`
- `STORAGE_FAILURE`
- `HID_KEY_DOWN_FAILED`
- `HID_KEY_RELEASE_FAILED`
- `HARDWARE_STOP`
- `INTERRUPTED_RESET`
- `WATCHDOG_RECOVERY`
- `RUNTIME_FAILURE`

An error response contains protocol version, request ID, operation, stable
error code, retryability, current state, and optional non-sensitive detail. It
does not include source text or schedule bytes.

## Planned Lifecycle

1. ESP32-CAM Bluetooth HID is paired and connected to the laptop.
2. The phone connects to the ESP32-CAM Wi-Fi endpoint.
3. The phone reads identity, firmware/protocol versions, capabilities, limits,
   storage information, and current state.
4. The phone creates a random 128-bit job ID.
5. The phone starts an upload and sends numbered retryable binary chunks.
6. Each chunk identifies job, sequence, byte offset, byte count, and CRC32.
7. An identical repeated chunk succeeds idempotently; a conflicting repeat is
   rejected with `CHUNK_CONFLICT`.
8. The board verifies encoded size, record count, all fields, and full payload
   SHA-256 before atomically marking the job READY.
9. START is rejected unless that job is READY and Bluetooth HID is connected.
10. The board sends and flushes the START acknowledgement.
11. The board keeps Wi-Fi and minimal HTTP control/status active, suspends mDNS,
    performs the eight-second countdown, and executes locally.
12. Stop may move COUNTDOWN or EXECUTING to STOPPING. The executor observes the
    stop flag between bounded waits, sends no later key-down, and releases all
    keyboard and consumer keys before reporting STOPPED.
13. The board retains a concise terminal result for the phone to poll.

Chunk CRC32 uses the standard reflected polynomial `0xEDB88320`, initial value
`0xFFFFFFFF`, and final XOR `0xFFFFFFFF`. CRC protects each transfer chunk;
payload SHA-256 remains the authoritative whole-job integrity check.

Progress is the executor's completed record count and source index. Android
polls it at a bounded interval and does not infer completion from time. There
is no remote Pause or Resume. Terminal completion or stop is confirmed only by
board status/result.

## Planned Control Operations

The later small control plane may use canonical JSON. Every request contains
`requestId`, operation, supported protocol versions, and fields listed below.
Every response echoes `requestId`, operation, board state, success, and either
result fields or the stable error object.

- `GET_IDENTITY`: returns board model, device ID, firmware version, and build.
- `GET_CAPABILITIES`: returns protocol/schedule versions, maximum schedule
  bytes, record count, individual delay, HID report form, offline-execution,
  hardware-stop, confirmed-progress, remote-control, and storage capabilities.
  Storage capabilities identify storage type, total capacity, free capacity,
  and maximum atomic staging-job size.
- `GET_STATUS`: returns state, active/ready job ID, received byte count, HID
  connection, and previous terminal result.
- `BEGIN_UPLOAD`: supplies job ID, encoded size, payload size/digest, action
  count, and expected duration. Repeating the same request is idempotent.
- `PUT_CHUNK`: supplies job ID, sequence, offset, byte count, CRC32, and binary
  body. Exact repeats are idempotent; conflicts fail.
- `FINALIZE_UPLOAD`: requests complete validation and atomic READY promotion.
- `START`: supplies the verified job ID. It is accepted once in READY and
  returns COUNTDOWN plus the authoritative 8000 ms duration.
- `STOP`: accepted in COUNTDOWN or EXECUTING and idempotent in STOPPING or
  STOPPED. It requests local stop and never fabricates terminal completion.
- `CANCEL`: accepted only in READY, invalidates the prepared job, returns IDLE,
  and emits no HID input.
- `GET_RESULT`: returns terminal state, stable code, completed record/source
  index, timing summary, and whether completion is confirmed.

There is no remote execution Pause/Resume operation. No operation silently
falls back to Phone Bluetooth.

## Storage And Recovery

Uploads go to staging storage. Only a fully validated job may be atomically
promoted READY. Incomplete, conflicting, previous, or partially verified data
is never executable. A reboot invalidates execution intent and never starts an
old job automatically. An atomic NVS active marker is set before countdown and
cleared only after a handled terminal outcome. A marker found at boot becomes
`INTERRUPTED_RESET`, is never resumed, and requires all-zero keyboard and
consumer reports after authenticated BLE reconnect before another START.

Firmware stores schedule records, job metadata, integrity values, and terminal
result, but never original source code. Schedule data is erased or
cryptographically invalidated after confirmed completion/abort according to a
documented retention policy. The previous non-sensitive terminal result remains
available after Wi-Fi returns. Execution is rejected whenever HID is not
connected.
