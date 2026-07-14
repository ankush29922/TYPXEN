# Changelog

All notable public prerelease changes are documented here.

## v1.0.0-rc3 - 2026-07-14

- Added authenticated, generation-bound reconnect safety readiness.
- Added bounded delayed retries that send only all-zero keyboard and consumer
  reports before another dedicated run may start.
- Added authoritative `startAllowed` status and a bounded manual safety-release
  endpoint.
- Kept the local Wi-Fi status and Stop path available during countdown and
  execution.
- Validated Prepare, READY, the eight-second countdown, Standard and Personal
  cadence, Stop, Cancel, progress, Bluetooth re-pairing, and interrupted-reset
  recovery on one original ESP32-CAM.
- Documented that simulated battery activity decrease remains experimental.

Earlier release-candidate packages are intentionally not distributed here.
