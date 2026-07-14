# ESP32-CAM Firmware Release Contract

No firmware artifact or firmware URL exists in Phase 2. The app board catalog
must keep release, source, setup, version, protocol, and checksum fields null
until a real release is produced and manually verified on hardware.

A future GitHub release must provide:

- a prebuilt merged `.bin` image;
- complete ESP-IDF source;
- the exact supported ESP-IDF version;
- firmware, protocol, and schedule-format versions;
- flash addresses and an exact `esptool` command;
- ESP32-CAM-MB flashing instructions;
- SHA-256 checksums for every downloadable artifact;
- erase, failed-flash, and recovery instructions;
- known board/module compatibility and limitations;
- a changelog and protocol compatibility statement.

GitHub Releases will distribute binaries, and GitHub documentation will explain
setup and flashing. The Typx APK will not bundle, generate, display, download,
or flash ESP-IDF firmware. No board becomes `supported` until pairing, upload,
integrity validation, offline execution, release safety, recovery, and result
reconnection have been manually tested on real hardware.
