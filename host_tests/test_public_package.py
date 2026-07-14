import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "package_public_release.py"
spec = importlib.util.spec_from_file_location("package_public_release", SCRIPT)
package = importlib.util.module_from_spec(spec)
assert spec.loader
spec.loader.exec_module(package)


class PublicPackageTests(unittest.TestCase):
    def test_release_contract_and_allowlist(self):
        self.assertEqual(package.VERSION, "1.0.0-rc3")
        self.assertEqual(
            package.BINARY_NAME,
            "typxen-esp32cam-v1.0.0-rc3-merged.bin",
        )
        self.assertTrue(package.is_public_source(Path("main/source.c")))
        self.assertFalse(package.is_public_source(Path("build/app.bin")))
        self.assertFalse(package.is_public_source(Path("sdkconfig")))
        self.assertFalse(package.is_public_source(Path("app-release.apk")))

    def test_private_scan_detects_paths_and_fixed_port(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "bad.txt").write_text(
                "C:" + "\\Users\\sample\\project and " + "COM" + "6",
                encoding="utf-8",
            )
            findings = package.private_findings(root)
            self.assertTrue(any("private Windows path" in item for item in findings))
            self.assertTrue(any("fixed COM-port" in item for item in findings))

    def test_public_docs_do_not_include_private_manual_bringup(self):
        self.assertNotIn("MANUAL_BLE_BRINGUP.md", package.PUBLIC_DOCS)
        self.assertIn("BLUETOOTH_IDENTITY_API.md", package.PUBLIC_DOCS)
        self.assertIn("RECOVERY.md", package.PUBLIC_DOCS)

    def test_verifier_is_required_in_public_package(self):
        script = ROOT / "tools" / "windows-board-setup" / "Verify-TypxFirmware.cmd"
        implementation = script.with_name("verify_typx_firmware.py")
        self.assertTrue(script.is_file())
        self.assertTrue(implementation.is_file())


if __name__ == "__main__":
    unittest.main()
