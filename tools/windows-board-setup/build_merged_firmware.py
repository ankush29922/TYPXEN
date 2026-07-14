#!/usr/bin/env python3
"""Build and verify one merged 4 MB image from ESP-IDF flash metadata."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD = ROOT / "targets" / "esp32_cam" / "build"
DEFAULT_OUTPUT = ROOT / "dist" / "typxen-esp32cam-v1.0.0-rc3-merged.bin"


def load_flash_metadata(build_dir: Path) -> dict:
    metadata_path = build_dir / "flasher_args.json"
    if not metadata_path.is_file():
        raise RuntimeError(f"ESP-IDF flash metadata is missing: {metadata_path}")
    return json.loads(metadata_path.read_text(encoding="utf-8"))


def merge_command(build_dir: Path, output: Path, metadata: dict) -> list[str]:
    chip = metadata["extra_esptool_args"]["chip"]
    command = [sys.executable, "-m", "esptool", "--chip", chip, "merge_bin"]
    command.extend(metadata["write_flash_args"])
    command.extend(["-o", str(output)])
    for offset, relative_file in sorted(
        metadata["flash_files"].items(), key=lambda item: int(item[0], 0)
    ):
        command.extend([offset, str(build_dir / relative_file)])
    return command


def verify_merged_image(build_dir: Path, output: Path, metadata: dict) -> None:
    merged = output.read_bytes()
    for offset, relative_file in metadata["flash_files"].items():
        start = int(offset, 0)
        source = (build_dir / relative_file).read_bytes()
        if merged[start : start + len(source)] != source:
            raise RuntimeError(f"Merged image mismatch at {offset}: {relative_file}")

    chip = metadata["extra_esptool_args"]["chip"]
    for section in ("bootloader", "app"):
        item = metadata[section]
        source = (build_dir / item["file"]).read_bytes()
        start = int(item["offset"], 0)
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as handle:
            inspection_image = Path(handle.name)
            handle.write(merged[start : start + len(source)])
        try:
            subprocess.run(
                [sys.executable, "-m", "esptool", "--chip", chip, "image_info", str(inspection_image)],
                check=True,
            )
        finally:
            inspection_image.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    metadata = load_flash_metadata(args.build_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        merge_command(args.build_dir, args.output, metadata), check=True
    )
    verify_merged_image(args.build_dir, args.output, metadata)
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
