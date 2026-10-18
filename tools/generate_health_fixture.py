"""Issue a short-lived loopback TLS fixture; never touch system trust stores.

The CA and key in ServiceHealthFixture.hpp are public test material. A fresh
server leaf keeps native TLS backends under their server-certificate lifetime
limits instead of using the long-lived test CA itself as a server identity.
OpenSSL is a build-tests tool only, not an application runtime dependency.
"""
import argparse
import hashlib
import json
import pathlib
import re
import secrets
import shutil
import subprocess
import tempfile
import time


def generate(source: pathlib.Path, destination: pathlib.Path, openssl: str) -> None:
    text = source.read_text(encoding="utf-8")
    digest = hashlib.sha256(text.encode() + pathlib.Path(__file__).read_bytes()).hexdigest()
    metadata = destination.with_suffix(".json")
    if destination.exists() and metadata.exists():
        try:
            cached = json.loads(metadata.read_text())
        except (ValueError, OSError):
            cached = {}
        if cached.get("source") == digest and cached.get("expires", 0) > time.time() + 86400:
            return
    command = shutil.which(openssl) or shutil.which("openssl")
    if not command:
        raise SystemExit("TLS fixture generation needs the OpenSSL command for build-tests.")

    def pem(name: str) -> str:
        match = re.search(rf'{name}\[\]\s*=\s*R"PEM\((.*?)\)PEM"', text, re.S)
        if not match:
            raise SystemExit("Public TLS fixture CA material is missing.")
        return match.group(1)

    def run(*arguments: str) -> None:
        result = subprocess.run([command, *arguments], capture_output=True, timeout=15)
        if result.returncode:
            raise SystemExit("OpenSSL failed to create the public loopback test identity.")

    with tempfile.TemporaryDirectory(prefix="greenrhythm-public-tls-fixture-") as folder:
        root = pathlib.Path(folder)
        ca, key, csr, leaf, traditional, extensions = [root / name for name in
            ("ca.pem", "ca.key", "leaf.csr", "leaf.pem", "leaf.key", "leaf.ext")]
        ca.write_text(pem("healthFixtureCaCertificate"))
        key.write_text(pem("healthFixtureCaKey"))
        extensions.write_text("basicConstraints=critical,CA:FALSE\n"
                              "keyUsage=critical,digitalSignature,keyEncipherment\n"
                              "extendedKeyUsage=serverAuth\n"
                              "subjectAltName=DNS:localhost,IP:127.0.0.1\n")
        run("req", "-new", "-key", str(key), "-subj", "/CN=localhost", "-out", str(csr))
        run("x509", "-req", "-in", str(csr), "-CA", str(ca), "-CAkey", str(key),
            "-set_serial", "0x" + secrets.token_hex(16), "-days", "30", "-sha256",
            "-extfile", str(extensions), "-out", str(leaf))
        version = subprocess.run([command, "version"], capture_output=True, text=True, timeout=5).stdout
        # LibreSSL and OpenSSL 1.x already default to PKCS#1 and do not all
        # accept OpenSSL 3's -traditional switch.
        legacy_switch = ["-traditional"] if re.match(r"OpenSSL [3-9]\.", version) else []
        run("rsa", "-in", str(key), *legacy_switch, "-out", str(traditional))
        run("verify", "-CAfile", str(ca), "-purpose", "sslserver", str(leaf))
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text('#pragma once\n// Generated public loopback test identity.\n'
            + 'inline constexpr char healthFixtureCertificate[] = R"PEM(' + leaf.read_text() + ')PEM";\n'
            + 'inline constexpr char healthFixtureKey[] = R"PEM(' + traditional.read_text() + ')PEM";\n')
        metadata.write_text(json.dumps({"source": digest, "expires": time.time() + 29 * 86400}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--openssl", default="openssl")
    args = parser.parse_args()
    generate(args.source, args.destination, args.openssl)
