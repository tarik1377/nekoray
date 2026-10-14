"""Verify and package the reserved, offline RU GeoIP snapshot.

CI uses the reviewed snapshot in the source tree. Online updates are performed
by the core's managed updater; packaging never replaces a user's geoip-ru.srs.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zlib


ASSET_DIRECTORY = Path(__file__).resolve().parent.parent / "res" / "geodata"
SRS_NAME = "geoip-ru-bootstrap.srs"
MANIFEST_NAME = "geoip-ru-bootstrap.json"
LICENSE_NAME = "LICENSE-CC-BY-SA-4.0.txt"


def validate_snapshot(asset_directory=ASSET_DIRECTORY):
    source = Path(asset_directory)
    manifest = json.loads((source / MANIFEST_NAME).read_text(encoding="utf-8"))
    if (manifest.get("schema") != 1 or manifest.get("country") != "RU"
            or manifest.get("license") != "CC-BY-SA-4.0"
            or not 10000 <= manifest.get("source_prefix_count", 0) <= 200000):
        raise ValueError("invalid RU GeoIP snapshot manifest")
    data = (source / SRS_NAME).read_bytes()
    if (len(data) != manifest.get("srs_bytes") or not 1000 <= len(data) <= 2 * 1024 * 1024
            or hashlib.sha256(data).hexdigest() != manifest.get("srs_sha256")):
        raise ValueError("RU GeoIP snapshot size or SHA256 mismatch")
    if data[:3] != b"SRS" or data[3] not in range(1, 5) or data[3] != manifest.get("srs_version"):
        raise ValueError("unsupported RU GeoIP snapshot format")
    decoder = zlib.decompressobj()
    plain = decoder.decompress(data[4:], 8 * 1024 * 1024 + 1)
    if (not plain or len(plain) > 8 * 1024 * 1024 or not decoder.eof
            or decoder.unused_data or decoder.unconsumed_tail):
        raise ValueError("invalid or oversized compressed RU GeoIP snapshot")
    license_text = (source / LICENSE_NAME).read_text(encoding="utf-8")
    if not license_text.startswith("Attribution-ShareAlike 4.0 International"):
        raise ValueError("missing RU GeoIP data license")
    return manifest


def package_snapshot(destination, asset_directory=ASSET_DIRECTORY, runtime_root=None):
    manifest = validate_snapshot(asset_directory)
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for name in (SRS_NAME, MANIFEST_NAME, LICENSE_NAME):
        shutil.copyfile(Path(asset_directory) / name, destination / name)
    if runtime_root is not None:
        # Updaters shipped before this policy accepted config/*.srs only. Plain
        # runtime text files already passed their install and receipt filters,
        # so first upgrades receive the data license and snapshot provenance too.
        runtime_root = Path(runtime_root)
        runtime_root.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(Path(asset_directory) / LICENSE_NAME, runtime_root / "GEODATA-LICENSE.txt")
        shutil.copyfile(Path(asset_directory) / MANIFEST_NAME, runtime_root / "GEOIP-RU-BOOTSTRAP.txt")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--runtime-root", type=Path)
    args = parser.parse_args()
    result = package_snapshot(args.destination, runtime_root=args.runtime_root)
    print(f"RU GeoIP bootstrap: {result['source_prefix_count']} prefixes, SHA256 {result['srs_sha256']}")
