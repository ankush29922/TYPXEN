#!/usr/bin/env python3
"""Verify the bundled merged Typxen firmware without accessing hardware."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "dist" / "RELEASE_MANIFEST.json"


class VerificationError(RuntimeError):
    pass


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def inspect_esp_image(data: bytes, target: str, name: str) -> None:
    with tempfile.TemporaryDirectory(prefix="typx-firmware-verify-") as temporary:
        image = Path(temporary) / f"{name}.bin"
        image.write_bytes(data)
        try:
            result = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "esptool",
                    "--chip",
                    target,
                    "image_info",
                    "--version",
                    "2",
                    str(image),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
        except OSError as error:
            raise VerificationError(f"Could not start esptool: {error}") from error
        if result.returncode != 0:
            detail = (result.stderr or result.stdout).strip()
            raise VerificationError(f"esptool rejected embedded {name}: {detail}")


def verify_release(manifest_path: Path = MANIFEST, inspect_images: bool = True) -> dict:
    require(manifest_path.is_file(), f"Release manifest is missing: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"Release manifest is invalid: {error}") from error

    binary_info = manifest.get("binary")
    images = manifest.get("embeddedImages")
    require(isinstance(binary_info, dict), "Manifest binary section is missing")
    require(isinstance(images, list) and images, "Manifest embeddedImages is missing")
    require(manifest.get("flashAddress") == "0x0", "Merged flash address must be 0x0")
    require(manifest.get("target") == "esp32", "Firmware target must be original ESP32")
    require(manifest.get("flashMode") == "dio", "Flash mode must be DIO")
    require(manifest.get("flashFrequency") == "40m", "Flash frequency must be 40 MHz")
    require(manifest.get("flashSize") == "4MB", "Flash size must be 4 MB")

    binary = manifest_path.parent / str(binary_info.get("file", ""))
    require(binary.is_file(), f"Merged firmware is missing: {binary}")
    data = binary.read_bytes()
    expected_size = binary_info.get("sizeBytes")
    expected_hash = str(binary_info.get("sha256", "")).lower()
    require(len(data) == expected_size, f"Merged size mismatch: {len(data)} != {expected_size}")
    actual_hash = sha256_bytes(data)
    require(actual_hash == expected_hash, f"Merged SHA-256 mismatch: {actual_hash}")

    ordered = sorted(images, key=lambda item: int(item["offset"], 0))
    cursor = 0
    for image in ordered:
        name = str(image.get("name", "embedded image"))
        offset = int(image["offset"], 0)
        size = int(image["sizeBytes"])
        require(offset >= cursor, f"Embedded ranges overlap before {name}")
        require(all(value == 0xFF for value in data[cursor:offset]),
                f"Non-0xFF padding before {name} at {image['offset']}")
        end = offset + size
        require(end <= len(data), f"Embedded {name} exceeds merged file size")
        embedded = data[offset:end]
        magic = bytes.fromhex(str(image["magicHex"]))
        require(embedded.startswith(magic),
                f"Embedded {name} magic mismatch at {image['offset']}")
        actual_embedded_hash = sha256_bytes(embedded)
        require(actual_embedded_hash == str(image["sha256"]).lower(),
                f"Embedded {name} SHA-256 mismatch")
        if inspect_images and image.get("esptoolImage") is True:
            inspect_esp_image(embedded, str(manifest["target"]), name)
        cursor = end
    require(cursor == len(data), "Unexpected data follows the final embedded image")
    return {
        "binary": binary,
        "size": len(data),
        "sha256": actual_hash,
        "images": ordered,
    }


def main() -> int:
    try:
        result = verify_release()
        print("PASS: Typxen merged firmware is valid")
        print(f"File: {result['binary']}")
        print(f"Size: {result['size']} bytes")
        print(f"SHA-256: {result['sha256'].upper()}")
        for image in result["images"]:
            print(
                f"{image['name']}: offset={image['offset']} "
                f"size={image['sizeBytes']} magic={image['magicHex'].upper()}"
            )
        print(
            "Note: direct image_info on the whole merged file is expected to fail "
            "because offset 0x0 contains 0xFF padding; ESP images begin later."
        )
        return 0
    except VerificationError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        print(
            "Install pinned verifier requirements with: "
            "py -3 -m pip install -r tools\\windows-board-setup\\requirements.txt",
            file=sys.stderr,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
