import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "windows-board-setup"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return module


setup = load("typx_board_setup", TOOL / "typx_board_setup.py")
merge = load("build_merged_firmware", TOOL / "build_merged_firmware.py")


class Port:
    def __init__(self, device, description):
        self.device = device
        self.description = description


class SetupTests(unittest.TestCase):
    def test_packaged_image_path_is_relative_to_repository(self):
        self.assertEqual(
            setup.IMAGE,
            ROOT / "dist" / "typxen-esp32cam-v1.0.0-rc3-merged.bin",
        )

    def test_port_selection_is_never_automatic(self):
        ports = [Port("COM42", "USB Serial")]
        with self.assertRaisesRegex(RuntimeError, "Nothing was flashed"):
            setup.select_port(ports, lambda _: "", lambda _: None)
        self.assertEqual(
            setup.select_port(ports, lambda _: "1", lambda _: None), "COM42"
        )

    def test_wrong_chip_is_rejected(self):
        self.assertTrue(setup.is_original_esp32("Chip is ESP32-D0WD-V3"))
        self.assertFalse(setup.is_original_esp32("Chip is ESP32-S3"))
        self.assertFalse(setup.is_original_esp32("Chip is ESP32-C3"))
        self.assertFalse(setup.is_original_esp32("no chip response"))

    def test_credentials_are_quoted_and_redacted(self):
        commands = setup.station_commands('My "Phone"', "safe pass\\word")
        self.assertEqual(
            commands,
            [
                'wifi-sta-config "My \\"Phone\\"" "safe pass\\\\word"',
                "wifi-sta-start",
                "wifi-info",
            ],
        )
        output = setup.redact_sensitive(
            "password=safe pass\\word", ["safe pass\\word"]
        )
        self.assertNotIn("safe pass", output)
        self.assertIn("<redacted>", output)

    def test_merge_command_uses_real_build_offsets(self):
        build = ROOT / "targets" / "esp32_cam" / "build"
        if not (build / "flasher_args.json").is_file():
            self.skipTest("ESP-IDF build metadata is not part of the public package")
        metadata = merge.load_flash_metadata(build)
        command = merge.merge_command(build, Path("merged.bin"), metadata)
        self.assertEqual(metadata["extra_esptool_args"]["chip"], "esp32")
        for offset, relative_file in metadata["flash_files"].items():
            self.assertIn(offset, command)
            self.assertIn(str(build / relative_file), command)
        self.assertEqual(
            set(metadata["flash_files"]), {"0x1000", "0x8000", "0x10000"}
        )


if __name__ == "__main__":
    unittest.main()
