# Phase 3 Firmware Architecture

## Board And Toolchain

The first target is an AI-Thinker-style ESP32-CAM used with an ESP32-CAM-MB
programmer. Its SoC target is the original dual-core Xtensa ESP32, not an
ESP32-S3. ESP-IDF is pinned to `v5.5.4`. Actual flash and PSRAM capabilities
must be detected at runtime in a later hardware phase; no seller specification
is trusted as a capability source.

## Protocol Provenance And Limits

Protocol V1 is copied byte-for-byte from the Android commit recorded in
`PROTOCOL_PROVENANCE.json`. The generic wire format allows 8,000,096 bytes and
250,000 records. The initial ESP32-CAM capability is deliberately lower:

- maximum encoded schedule: 512 KiB (524,288 bytes);
- header: 96 bytes;
- maximum records: 16,381;
- maximum individual delay: 600,000,000 microseconds.

The caller supplies limits to the decoder, keeping board capability separate
from wire-format limits. The decoder uses a random-access reader, fixed stack
buffers, explicit little-endian reads, overflow checks, streaming SHA-256, and
on-demand record decoding. It never allocates from untrusted header values and
does not require a complete schedule in internal RAM or PSRAM.

Uploads will eventually be written to staging storage and locked while a
verified schedule is executable. Incomplete or mutated staging data cannot be
executed. Original source code is never stored.

## Executor Safety

The executor accepts only the decoder's verified-schedule token. Clock, wait,
HID key-down, HID release-all, stop, progress, and terminal reporting are
injected. Every record executes wait-before, key-down, encoded hold,
release-all, progress, and wait-after-release. No allocation occurs in this
loop.

Release-all is attempted after every key-down and on completion, stop, wait
failure, key-down failure, record-read failure, and unverified-schedule error.
Progress is emitted only after successful release. Standard and Personal
schedules use exactly the same executor. Personal cadence and the 60 ms release
settle are already ordinary resolved schedule timing; firmware does not
resample or reinterpret them.

The encoded 8 ms key hold is requested by the phone but is **not yet hardware
certified**. Firmware must not silently modify it. The Bluetooth phase must
either validate it under real HID conditions or advertise a minimum hold so
Android can compile a compatible schedule before upload.

## Intentional Absences

Phase 3 starts no Wi-Fi or Bluetooth stack and accesses no camera or microSD.
This keeps protocol parsing, verification, state safety, and executor ordering
testable before radio lifecycle complexity exists.

The next phase is Bluetooth HID bring-up and key-hold/release reliability
testing on the physical ESP32-CAM. A later phase will add staged Wi-Fi schedule
transport and Android integration after executor safety is proven.
