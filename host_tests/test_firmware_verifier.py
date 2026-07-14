import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "windows-board-setup" / "verify_typx_firmware.py"
spec = importlib.util.spec_from_file_location("verify_typx_firmware", SCRIPT)
verifier = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(verifier)


class FirmwareVerifierTests(unittest.TestCase):
    def test_public_release_layout_passes_without_hardware(self):
        manifest = ROOT.parent / "Typxen-ESP32CAM-Firmware-Public" / "dist" / "RELEASE_MANIFEST.json"
        if not manifest.is_file() or "embeddedImages" not in json.loads(manifest.read_text()):
            self.skipTest("Public manifest has not been refreshed yet")
        result = verifier.verify_release(manifest, inspect_images=False)
        self.assertEqual(
            result["sha256"],
            verifier.sha256_bytes(result["binary"].read_bytes()),
        )
        self.assertEqual([item["offset"] for item in result["images"]], ["0x1000", "0x8000", "0x10000"])

    def test_padding_and_magic_failures_are_precise(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "firmware.bin"
            data = bytearray([0xFF] * 0x20)
            data[0x10:0x12] = b"\xE9\x00"
            binary.write_bytes(data)
            manifest = {
                "target": "esp32",
                "flashAddress": "0x0",
                "flashMode": "dio",
                "flashFrequency": "40m",
                "flashSize": "4MB",
                "binary": {
                    "file": binary.name,
                    "sizeBytes": len(data),
                    "sha256": verifier.sha256_bytes(data),
                },
                "embeddedImages": [{
                    "name": "application",
                    "offset": "0x10",
                    "sizeBytes": 0x10,
                    "sha256": verifier.sha256_bytes(data[0x10:]),
                    "magicHex": "e9",
                    "esptoolImage": False,
                }],
            }
            manifest_path = root / "RELEASE_MANIFEST.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            verifier.verify_release(manifest_path, inspect_images=False)

            broken = copy.deepcopy(manifest)
            broken["embeddedImages"][0]["magicHex"] = "aa50"
            manifest_path.write_text(json.dumps(broken), encoding="utf-8")
            with self.assertRaisesRegex(verifier.VerificationError, "magic mismatch"):
                verifier.verify_release(manifest_path, inspect_images=False)


if __name__ == "__main__":
    unittest.main()
