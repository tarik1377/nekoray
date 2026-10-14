import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest
import zlib

import geodata_bootstrap as snapshot


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / "assets"
        shutil.copytree(snapshot.ASSET_DIRECTORY, self.assets)

    def manifest(self):
        return json.loads((self.assets / snapshot.MANIFEST_NAME).read_text())

    def set_binary(self, data):
        (self.assets / snapshot.SRS_NAME).write_bytes(data)
        manifest = self.manifest()
        manifest["srs_bytes"] = len(data)
        manifest["srs_sha256"] = hashlib.sha256(data).hexdigest()
        (self.assets / snapshot.MANIFEST_NAME).write_text(json.dumps(manifest))

    def test_actual_reviewed_snapshot_has_matching_manifest(self):
        manifest = snapshot.validate_snapshot(self.assets)
        self.assertEqual(manifest["source_prefix_count"], 25171)

    def test_windows_and_macos_destinations_keep_legacy_user_rule_set(self):
        for relative in ("windows64/config", "GreenRhythm.app/Contents/MacOS/config"):
            with self.subTest(destination=relative):
                destination = self.root / relative
                destination.mkdir(parents=True)
                legacy = destination / "geoip-ru.srs"
                legacy.write_bytes(b"user-maintained-local-rule-set")
                snapshot.package_snapshot(destination, self.assets, runtime_root=destination.parent)
                self.assertEqual(legacy.read_bytes(), b"user-maintained-local-rule-set")
                for name in (snapshot.SRS_NAME, snapshot.MANIFEST_NAME, snapshot.LICENSE_NAME):
                    self.assertEqual((destination / name).read_bytes(), (self.assets / name).read_bytes())
                self.assertEqual((destination.parent / "GEODATA-LICENSE.txt").read_bytes(),
                                 (self.assets / snapshot.LICENSE_NAME).read_bytes())
                self.assertEqual((destination.parent / "GEOIP-RU-BOOTSTRAP.txt").read_bytes(),
                                 (self.assets / snapshot.MANIFEST_NAME).read_bytes())

    def test_tampered_binary_never_reaches_output(self):
        (self.assets / snapshot.SRS_NAME).write_bytes(b"<html>download error</html>")
        destination = self.root / "output"
        with self.assertRaises(ValueError):
            snapshot.package_snapshot(destination, self.assets)
        self.assertFalse(destination.exists())

    def test_bad_compression_rejected_even_with_matching_checksum(self):
        self.set_binary(b"SRS\x01" + b"not-compressed" * 100)
        with self.assertRaises(zlib.error):
            snapshot.validate_snapshot(self.assets)

    def test_trailing_compressed_data_rejected(self):
        data = (self.assets / snapshot.SRS_NAME).read_bytes()
        self.set_binary(data + b"unexpected")
        with self.assertRaises(ValueError):
            snapshot.validate_snapshot(self.assets)

    def test_missing_license_is_a_packaging_failure(self):
        (self.assets / snapshot.LICENSE_NAME).unlink()
        with self.assertRaises(FileNotFoundError):
            snapshot.package_snapshot(self.root / "output", self.assets)
        self.assertFalse((self.root / "output").exists())


if __name__ == "__main__":
    unittest.main()
