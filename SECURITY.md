# Security Policy

## Supported version

Security fixes currently target the latest published prerelease only:
`v1.0.0-rc3`.

## Reporting a vulnerability

Do not include credentials, licence tokens, private keys, customer data, source
captured by Typx, or recorder sessions in a public issue. Use GitHub's private
vulnerability reporting flow on this repository when available. If that option
is unavailable, open a minimal issue asking the maintainer to enable a private
channel; do not disclose exploit details in the issue.

Include the affected version, mode, original ESP32-CAM hardware details, Android
and Windows versions, reproducible steps, and the safety impact. Redact SSIDs,
passwords, MAC addresses, IP addresses, device codes, licence tokens, and local
filesystem paths.

## Operational safety

Verify release SHA-256 values before installing or flashing. Do not flash an
untested board variant. If keyboard state appears stuck, follow
[Recovery](docs/RECOVERY.md) before attempting another run.
