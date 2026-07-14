#!/usr/bin/env python3
"""Interactive, credential-safe Typx ESP32-CAM setup for Windows."""

from __future__ import annotations

import getpass
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Callable, Iterable


ROOT = Path(__file__).resolve().parents[2]
IMAGE = ROOT / "dist" / "typxen-esp32cam-v1.0.0-rc3-merged.bin"
INSTALL = (
    f'Install requirements with: py -3 -m pip install -r '
    f'"{Path(__file__).with_name("requirements.txt")}"'
)


def load_dependencies():
    try:
        import serial  # type: ignore
        from serial.tools import list_ports  # type: ignore
    except ImportError as error:
        raise RuntimeError(INSTALL) from error
    try:
        import esptool  # noqa: F401  # type: ignore
    except ImportError as error:
        raise RuntimeError(INSTALL) from error
    return serial, list_ports


def select_port(
    ports: Iterable[object],
    input_fn: Callable[[str], str] = input,
    output: Callable[[str], None] = print,
) -> str:
    available = list(ports)
    if not available:
        raise RuntimeError("No serial ports found. Connect ESP32-CAM-MB and retry.")
    output("Available serial ports:")
    for index, port in enumerate(available, 1):
        device = str(getattr(port, "device", ""))
        description = str(getattr(port, "description", "Unknown device"))
        output(f"  {index}. {device}  {description}")
    raw = input_fn("Select the ESP32-CAM-MB port number: ").strip()
    if not raw.isdigit() or not 1 <= int(raw) <= len(available):
        raise RuntimeError("No port selected. Nothing was flashed.")
    return str(getattr(available[int(raw) - 1], "device"))


def is_original_esp32(probe_output: str) -> bool:
    match = re.search(r"Chip is\s+([^\r\n]+)", probe_output, re.IGNORECASE)
    if not match:
        return False
    chip = match.group(1).upper()
    if not chip.startswith("ESP32"):
        return False
    return not any(
        variant in chip
        for variant in ("ESP32-S2", "ESP32-S3", "ESP32-C", "ESP32-H", "ESP32-P4")
    )


def run_esptool(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "esptool", *arguments],
        check=False,
        capture_output=True,
        text=True,
    )


def quote_uart_argument(value: str) -> str:
    if any(character in value for character in "\r\n\0"):
        raise ValueError("SSID and password cannot contain control characters.")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def station_commands(ssid: str, password: str) -> list[str]:
    if not 1 <= len(ssid.encode("utf-8")) <= 32:
        raise ValueError("Hotspot SSID must be 1 to 32 UTF-8 bytes.")
    if not 8 <= len(password.encode("utf-8")) <= 63:
        raise ValueError("Hotspot password must be 8 to 63 UTF-8 bytes.")
    return [
        f"wifi-sta-config {quote_uart_argument(ssid)} {quote_uart_argument(password)}",
        "wifi-sta-start",
        "wifi-info",
    ]


def redact_sensitive(text: str, secrets: Iterable[str]) -> str:
    redacted = text
    for secret in secrets:
        if secret:
            redacted = redacted.replace(secret, "<redacted>")
    return redacted


def read_until_prompt(connection, timeout: float = 12.0) -> str:
    deadline = time.monotonic() + timeout
    data = bytearray()
    while time.monotonic() < deadline:
        waiting = connection.in_waiting
        if waiting:
            data.extend(connection.read(waiting))
            if b"typx>" in data:
                break
        else:
            time.sleep(0.05)
    return data.decode("utf-8", errors="replace")


def send_uart_command(connection, command: str) -> str:
    connection.reset_input_buffer()
    connection.write((command + "\r\n").encode("utf-8"))
    connection.flush()
    return read_until_prompt(connection)


def main() -> int:
    try:
        serial, list_ports = load_dependencies()
        port = select_port(list_ports.comports())
        probe = run_esptool(["--chip", "auto", "--port", port, "chip_id"])
        probe_text = probe.stdout + probe.stderr
        if probe.returncode != 0 or not is_original_esp32(probe_text):
            raise RuntimeError(
                "Selected device is not an original ESP32. Nothing was flashed."
            )
        confirmation = input(
            "Original ESP32 detected. Type ESP32 to confirm flashing this device: "
        ).strip()
        if confirmation != "ESP32":
            raise RuntimeError("Confirmation was not provided. Nothing was flashed.")
        if not IMAGE.is_file():
            raise RuntimeError(
                f"Prepared firmware image is missing: {IMAGE}\n"
                "Ask the distributor for the merged Typx firmware image."
            )
        flashed = run_esptool(
            [
                "--chip", "esp32", "--port", port,
                "--before", "default_reset", "--after", "hard_reset",
                "write_flash", "--flash_size", "4MB", "0x0", str(IMAGE),
            ]
        )
        if flashed.returncode != 0:
            raise RuntimeError("Firmware flashing failed. Nothing else was configured.")
        time.sleep(3.0)
        ssid = input("Phone-hotspot name: ")
        password = getpass.getpass("Phone-hotspot password: ")
        commands = station_commands(ssid, password)
        with serial.Serial(port, 115200, timeout=0.2, write_timeout=2) as connection:
            connection.dtr = False
            connection.rts = False
            time.sleep(1.0)
            read_until_prompt(connection, 3.0)
            configured = send_uart_command(connection, commands[0])
            if "wifi-sta-config completed" not in configured:
                raise RuntimeError("Station configuration was not accepted.")
            started = send_uart_command(connection, commands[1])
            if "wifi-sta-start completed" not in started:
                raise RuntimeError("Station mode could not be started.")
            send_uart_command(connection, commands[2])
        print("Keep your phone hotspot enabled whenever you use Typx.")
        print("ESP32-CAM setup complete.")
        print(
            "Enable your phone hotspot, pair Typxen ESP32-CAM with the laptop, "
            "then open Typx."
        )
        return 0
    except (RuntimeError, ValueError) as error:
        print(redact_sensitive(f"Setup stopped: {error}", []), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
