# ESP32-CAM Schedule Protocol V1

## Scope

This document defines the language-neutral schedule contract planned between
Typx Android and dedicated ESP32-CAM firmware. Phase 2 implements only a pure
TypeScript codec and validator. It does not implement transport, discovery,
firmware, or runtime integration.

Android remains the authority for source normalization, editor planning, HID
mapping, Standard timing, Personal Cadence sampling, newline actions, and final
cleanup. The board receives only finalized HID records. It never receives or
parses source code, editor rules, or the Personal Cadence profile.

## Integer Timing

Every delay is an unsigned integer number of microseconds. A non-negative
finite millisecond value is converted with `Math.round(milliseconds * 1000)`:
nearest microsecond, with an exact half rounded upward. Floating-point values
are never transmitted. Inputs that are negative, non-finite, above 600,000 ms,
or outside the exact integer range are rejected before encoding.

The expected duration is the exact sum of each record's wait-before, key hold,
and wait-after values. It excludes countdown, transfer, release retries,
recovery, and reconnection time.

## Byte Order And Header

All multi-byte integers are little-endian. The header is exactly 96 bytes.

| Offset | Size | Field | V1 requirement |
| ---: | ---: | --- | --- |
| 0 | 8 | Magic | ASCII `TYPXHID1` |
| 8 | 1 | Protocol major | `1`; other values are incompatible |
| 9 | 1 | Protocol minor | `0`; additive compatible evolution only |
| 10 | 1 | Schedule format | `1` |
| 11 | 1 | Byte-order marker | `1` for little-endian |
| 12 | 2 | Header length | `96` |
| 14 | 2 | Record length | `32` |
| 16 | 4 | Required flags | `0x00000003` exactly |
| 20 | 4 | Action count | Number of 32-byte records |
| 24 | 4 | Payload length | `actionCount * 32` |
| 28 | 4 | Source character count | Positive planner source length |
| 32 | 8 | Expected duration | Exact unsigned microseconds |
| 40 | 16 | Job ID | Random 128-bit value, not all zero |
| 56 | 32 | Payload SHA-256 | Digest of record payload only |
| 88 | 8 | Reserved | All zero |

Required flag bit 0 states that final cleanup metadata is present. Bit 1 states
that every key-down is followed by release-all. Missing or unknown bits are an
error. V1 accepts at most 250,000 records and 8,000,096 encoded bytes.

The full encoded length must equal `headerLength + payloadLength`. Truncation,
trailing bytes, multiplication inconsistencies, and integer overflow are
errors. The complete payload SHA-256 is verified before any execution.

## Fixed Keystroke Record

Each payload record is exactly 32 bytes.

| Offset | Size | Field | V1 requirement |
| ---: | ---: | --- | --- |
| 0 | 4 | Wait before key-down | Microseconds, maximum 600,000,000 |
| 4 | 4 | Key-down hold | Exactly 8,000 microseconds |
| 8 | 4 | Wait after release | Microseconds, maximum 600,000,000 |
| 12 | 4 | Source/progress index | Monotonic, within source count |
| 16 | 1 | HID modifier | Current bits `0x01` Control, `0x02` Shift only |
| 17 | 1 | HID keycode | Current validated Typx US-English/special set |
| 18 | 2 | Record flags | Defined below |
| 20 | 1 | Action kind | `1`, one-key boot-keyboard report |
| 21 | 1 | Progress policy | `1`, update after successful release |
| 22 | 10 | Reserved | All zero |

Record flags are `0x0001` source action, `0x0002` technical action, `0x0004`
final cleanup, and `0x0008` mandatory release-all. Exactly one of source or
technical is required. Final-cleanup records must be technical. Unknown bits
are rejected. Bytes 20-31 reserve room for future report forms without making
V1 variable-length.

V1 keycodes are restricted to those currently emitted by `HidKeyMapper`: USB
HID usages `0x04-0x31`, `0x33-0x38`, Home `0x4A`, Delete `0x4C`, End `0x4D`,
and Arrow Right `0x4F`. This includes Escape, Enter, Backspace, ordinary shifted
US-English characters, and the current shortcuts.

## Runtime Mapping

The audited native executor order is preserved as follows:

1. Wait `waitBeforeKeyDownUs`.
2. Send `{modifier, keycode}` with all other key slots empty.
3. Hold for `keyDownHoldUs` (8 ms).
4. Send and confirm an empty HID report (release-all).
5. Advance progress to `sourceIndex`.
6. Wait `waitAfterReleaseUs`.

For Standard mode, Android selects the active speed and resolves the existing
`standardDelayChoices` into `waitAfterReleaseUs` before serialization. Offline
execution therefore has no mid-job speed change.

For Personal mode, Android creates one seed and one immutable overlay. The
overlay's pre-delay is resolved on the phone. The current 60 ms post-release
settle requirement is represented as any additional wait needed before the
next key-down after accounting for the previous wait-after and current cadence
wait. Key hold remains exactly 8 ms and is never replaced by cadence timing.
Only resolved integers are sent: no seed, source text, profile, categories, or
sampling decisions are placed on the wire.

The last three records must be technical records, marked final cleanup, with
exact HID reports: Escape; Control+Shift+End; Delete. A missing or differently
marked cleanup is invalid. Newline handling remains Android-planned and appears
in source order as Escape; Enter; Shift+Home; Shift+Home; Delete.

## Validation And Execution Safety

Firmware must stage the entire upload and validate magic, versions, sizes,
flags, reserved bytes, count, limits, indexes, keycodes, modifiers, timing,
cleanup metadata, total duration, and SHA-256 before making a job READY. A
malformed schedule is never partially executed.

Firmware must send an empty HID report after every key-down and also on normal
completion, abort, validation/runtime error, watchdog recovery, and, where
possible, before reboot or Bluetooth disconnection. Failure to release is a
terminal error, never a reason to continue.

The TypeScript validator uses stable codes including `VERSION_INCOMPATIBLE`,
`HEADER_INVALID`, `PAYLOAD_SIZE_INVALID`, `ACTION_COUNT_MISMATCH`,
`CHECKSUM_MISMATCH`, `MODIFIER_INVALID`, `KEYCODE_UNSUPPORTED`, `DELAY_INVALID`,
`SOURCE_INDEX_INVALID`, `FINAL_CLEANUP_INVALID`, `FLAGS_INVALID`,
`RESERVED_FIELD_NONZERO`, `TOTAL_DURATION_INVALID`, and
`SCHEDULE_LIMIT_EXCEEDED`.

## Golden Fixtures

Privacy-safe fixtures are under
`__tests__/fixtures/hardware-protocol-v1/`. `vectors.json` documents each
binary's job ID, records, duration, payload digest, and whole-file digest. It
contains Standard, deterministic Personal, newline/final-cleanup, corruption,
truncation, incompatible-version, modifier, reserved-field, and encode-only
overflow cases. Future ESP-IDF tests must reproduce valid files byte-for-byte
and reject malformed files with equivalent stable errors.
