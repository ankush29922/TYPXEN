#!/usr/bin/env python3
"""Create and verify the standalone Typxen ESP32-CAM source release."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DESTINATION = ROOT.parent / "Typxen-ESP32CAM-Firmware-Public"
BUILD_DIR = ROOT / "targets" / "esp32_cam" / "build"
VERSION = "1.0.0-rc3"
BINARY_NAME = f"typxen-esp32cam-v{VERSION}-merged.bin"

TOP_LEVEL_FILES = (
    ".gitignore",
    ".gitattributes",
    ".idf-version",
    "README.md",
    "CHANGELOG.md",
    "CONTRIBUTING.md",
    "SECURITY.md",
    "SHA256SUMS-v1.0.0-rc3.txt",
    "PROTOCOL_PROVENANCE.json",
)
SOURCE_DIRECTORIES = (
    ".github",
    "components",
    "targets",
    "tools/windows-board-setup",
    "host_tests",
    "test_vectors",
)
PUBLIC_DOCS = (
    "BLE_HID_VALIDATION.md",
    "LOCAL_SCHEDULE_VALIDATION.md",
    "PHASE3_ARCHITECTURE.md",
    "WIFI_UPLOAD.md",
    "BLUETOOTH_IDENTITY_API.md",
    "HTTP_API.md",
    "DEVICE_SETUP.md",
    "DAILY_USE.md",
    "APP_INSTALLATION.md",
    "FIRMWARE_FLASHING.md",
    "HARDWARE_COMPATIBILITY.md",
    "INDEX.md",
    "PROTOCOL.md",
    "TROUBLESHOOTING.md",
    "RECOVERY.md",
    "RELEASE_NOTES_v1.0.0-rc3.md",
    "RC3_HARDWARE_VALIDATION.md",
)
EXCLUDED_NAMES = {
    ".git",
    ".idea",
    ".vscode",
    "__pycache__",
    "build",
    "managed_components",
    "sdkconfig",
    "sdkconfig.old",
    "dependencies.lock",
    "secrets",
    "credentials",
}
EXCLUDED_SUFFIXES = {
    ".a",
    ".apk",
    ".elf",
    ".exe",
    ".log",
    ".map",
    ".o",
    ".obj",
    ".pyc",
    ".tmp",
}
PRIVATE_PATTERNS = (
    (re.compile(b"xen" + b"29", re.IGNORECASE), "personal identifier"),
    (re.compile(rb"[A-Za-z]:\\Users\\", re.IGNORECASE), "private Windows path"),
    (re.compile(b"COM" + b"6", re.IGNORECASE), "fixed COM-port assumption"),
    (re.compile(rb"(?:10|127)\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}"), "local IP address"),
    (re.compile(rb"192\.168\.[0-9]{1,3}\.[0-9]{1,3}"), "local IP address"),
)
TEXT_SUFFIXES = {
    "", ".c", ".cmd", ".csv", ".h", ".json", ".md", ".ps1",
    ".py", ".txt", ".yml",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_public_source(path: Path) -> bool:
    if any(part in EXCLUDED_NAMES for part in path.parts):
        return False
    return path.suffix.lower() not in EXCLUDED_SUFFIXES


def copy_directory(source: Path, destination: Path) -> None:
    for item in source.rglob("*"):
        relative = item.relative_to(source)
        if not is_public_source(relative):
            continue
        target = destination / relative
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif item.is_file():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, target)


def flash_value(arguments: list[str], name: str) -> str:
    try:
        return arguments[arguments.index(name) + 1]
    except (ValueError, IndexError) as error:
        raise RuntimeError(f"Missing {name} in flasher_args.json") from error


def application_partition_size() -> int:
    partition_file = ROOT / "targets" / "esp32_cam" / "partitions.csv"
    with partition_file.open(encoding="utf-8", newline="") as handle:
        for row in csv.reader(line for line in handle if not line.startswith("#")):
            if row and row[0].strip() == "factory":
                return int(row[4].strip(), 0)
    raise RuntimeError("Factory application partition is missing")


def build_manifest(binary: Path) -> dict:
    metadata = json.loads((BUILD_DIR / "flasher_args.json").read_text(encoding="utf-8"))
    arguments = metadata["write_flash_args"]
    app = BUILD_DIR / metadata["app"]["file"]
    partition_size = application_partition_size()
    app_size = app.stat().st_size
    embedded_images = []
    sections = (
        ("bootloader", metadata["bootloader"], "e9", True),
        ("partition-table", metadata["partition-table"], "aa50", False),
        ("application", metadata["app"], "e9", True),
    )
    for name, section, magic, esptool_image in sections:
        artifact = BUILD_DIR / section["file"]
        embedded_images.append({
            "name": name,
            "offset": section["offset"],
            "sourceFile": section["file"].replace("\\", "/"),
            "sizeBytes": artifact.stat().st_size,
            "sha256": sha256(artifact),
            "magicHex": magic,
            "esptoolImage": esptool_image,
        })
    return {
        "release": f"v{VERSION}",
        "status": "Release Candidate - core workflow manually validated",
        "firmwareVersion": VERSION,
        "idfVersion": "v5.5.4",
        "target": metadata["extra_esptool_args"]["chip"],
        "board": "original ESP32 / ESP32-CAM",
        "flashAddress": "0x0",
        "flashMode": flash_value(arguments, "--flash_mode"),
        "flashFrequency": flash_value(arguments, "--flash_freq"),
        "flashSize": flash_value(arguments, "--flash_size"),
        "protocolVersion": "1.0",
        "scheduleFormatVersion": 1,
        "validation": {
            "date": "2026-07-14",
            "scope": "one original ESP32-CAM",
            "coreTypingAndSafety": "passed",
            "batteryActivityDecrease": "experimental-not-observed",
            "broadHardwareCompatibilityClaimed": False,
        },
        "application": {
            "sizeBytes": app_size,
            "partitionSizeBytes": partition_size,
            "freeBytes": partition_size - app_size,
        },
        "binary": {
            "file": binary.name,
            "sizeBytes": binary.stat().st_size,
            "sha256": sha256(binary),
        },
        "embeddedImages": embedded_images,
    }


def private_findings(root: Path) -> list[str]:
    findings: list[str] = []
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if any(part == ".git" for part in relative.parts):
            findings.append(f"Git metadata: {relative}")
            continue
        if path.is_dir():
            continue
        if path.suffix.lower() == ".apk" or path.name in {"sdkconfig", "sdkconfig.old"}:
            findings.append(f"forbidden artifact: {relative}")
        data = path.read_bytes()
        patterns = (
            PRIVATE_PATTERNS
            if path.suffix.lower() in TEXT_SUFFIXES
            else PRIVATE_PATTERNS[:3]
        )
        for pattern, label in patterns:
            if pattern.search(data):
                findings.append(f"{label}: {relative}")
    return findings


def verify_package(destination: Path) -> None:
    required = (
        "README.md",
        "CHANGELOG.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "SHA256SUMS-v1.0.0-rc3.txt",
        ".gitignore",
        ".gitattributes",
        ".idf-version",
        "components/typx_protocol/include/typx_protocol.h",
        "components/typx_ble_identity/include/typx_ble_identity.h",
        "components/typx_execution_safety/include/typx_execution_safety.h",
        "targets/esp32_cam/CMakeLists.txt",
        "targets/esp32_cam/sdkconfig.defaults",
        "targets/esp32_cam/partitions.csv",
        "tools/windows-board-setup/Setup-TypxBoard.cmd",
        "tools/windows-board-setup/Verify-TypxFirmware.cmd",
        "tools/windows-board-setup/verify_typx_firmware.py",
        "tools/windows-board-setup/requirements.txt",
        "docs/BLUETOOTH_IDENTITY_API.md",
        "docs/APP_INSTALLATION.md",
        "docs/FIRMWARE_FLASHING.md",
        "docs/HARDWARE_COMPATIBILITY.md",
        "docs/RELEASE_NOTES_v1.0.0-rc3.md",
        "docs/RC3_HARDWARE_VALIDATION.md",
        "docs/protocol/ESP32_CAM_PROTOCOL_V1.md",
        "host_tests/test_ble_identity.c",
        "host_tests/test_execution_safety.c",
        "test_vectors/protocol-v1/vectors.json",
        f"dist/{BINARY_NAME}",
        "dist/SHA256SUMS.txt",
        "dist/RELEASE_MANIFEST.json",
        "dist/RELEASE_NOTES.md",
    )
    missing = [item for item in required if not (destination / item).is_file()]
    if missing:
        raise RuntimeError("Missing public release files: " + ", ".join(missing))
    setup_text = (destination / "tools/windows-board-setup/typx_board_setup.py").read_text(encoding="utf-8")
    if BINARY_NAME not in setup_text:
        raise RuntimeError("Windows setup tool does not reference the bundled image")
    findings = private_findings(destination)
    if findings:
        raise RuntimeError("Public package privacy check failed:\n" + "\n".join(findings))


def package(destination: Path, binary: Path) -> None:
    if destination.exists():
        raise RuntimeError(f"Destination already exists: {destination}")
    if not binary.is_file():
        raise RuntimeError(f"Merged firmware image is missing: {binary}")
    destination.mkdir(parents=True)
    for name in TOP_LEVEL_FILES:
        shutil.copy2(ROOT / name, destination / name)
    for name in SOURCE_DIRECTORIES:
        copy_directory(ROOT / name, destination / name)
    shutil.copy2(Path(__file__), destination / "tools" / Path(__file__).name)
    for name in PUBLIC_DOCS:
        source = ROOT / "docs" / name
        target = destination / "docs" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    copy_directory(ROOT / "docs" / "protocol", destination / "docs" / "protocol")

    dist = destination / "dist"
    dist.mkdir()
    packaged_binary = dist / BINARY_NAME
    shutil.copy2(binary, packaged_binary)
    shutil.copy2(
        ROOT / "docs" / "RELEASE_NOTES_v1.0.0-rc3.md",
        dist / "RELEASE_NOTES.md",
    )
    shutil.copy2(
        ROOT / "docs" / "RELEASE_NOTES_v1.0.0-rc3.md",
        dist / "RELEASE_NOTES_v1.0.0-rc3.md",
    )
    manifest = build_manifest(packaged_binary)
    manifest_path = dist / "RELEASE_MANIFEST.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    checksummed = (
        packaged_binary,
        manifest_path,
        dist / "RELEASE_NOTES.md",
        dist / "RELEASE_NOTES_v1.0.0-rc3.md",
    )
    (dist / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256(path)}  {path.name}\n" for path in checksummed),
        encoding="utf-8",
    )
    verify_package(destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    parser.add_argument("--binary", type=Path, default=ROOT / "dist" / BINARY_NAME)
    args = parser.parse_args()
    package(args.destination.resolve(), args.binary.resolve())
    print(args.destination.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
