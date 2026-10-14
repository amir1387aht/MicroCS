#!/usr/bin/env python3
"""Own ordinary fixtures for offline preview byte/metadata verification."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_preview as V

VERSION = "2.0.0-preview.1"
COMMIT = "a" * 40
TREE = "b" * 40


class VerifyPreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.bundle = Path(self.temp.name) / "bundle"
        self.bundle.mkdir()
        self.stem = f"MicroCS-{VERSION}-g{COMMIT[:12]}"
        self.manifest = {
            "format": "microcs-preview-manifest", "version": 1, "channel": "preview",
            "package_version": VERSION, "runtime_version": VERSION,
            "source_commit": COMMIT, "source_tree": TREE, "source_epoch": 1700000000,
            "host": None, "firmware_included": False, "host_capabilities": None,
            "packaging_environment": {"system": "Linux", "machine": "x86_64", "libc": ["glibc", "2.36"], "python": "3.13"},
            "graphics": "experimental", "security": "not a sandbox", "publication": "candidate only",
            "artifacts": {},
        }
        for suffix in ("-source.tar.gz", "-studio.zip"):
            self.add_artifact(suffix)
        self.write_metadata()

    def tearDown(self):
        self.temp.cleanup()

    def add_artifact(self, suffix):
        # Intentionally opaque ordinary bytes, not a real archive: verification
        # must not attempt to open/extract an archive or run its contents.
        name = self.stem + suffix
        data = ("own ordinary fixture " + suffix).encode()
        (self.bundle / name).write_bytes(data)
        self.manifest["artifacts"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        return name

    def write_metadata(self):
        (self.bundle / "manifest.json").write_text(json.dumps(self.manifest) + "\n", encoding="utf-8")
        self.write_checks()

    def write_checks(self):
        names = set(self.manifest["artifacts"]) | {"manifest.json"}
        lines = []
        for name in sorted(names):
            if (self.bundle / name).is_file():
                lines.append(f"{hashlib.sha256((self.bundle / name).read_bytes()).hexdigest()}  {name}\n")
        (self.bundle / "SHA256SUMS").write_text("".join(lines), encoding="utf-8")

    def snapshot(self):
        return {p.name: (p.read_bytes(), p.stat().st_mode, p.stat().st_mtime_ns, p.stat().st_ctime_ns)
                for p in self.bundle.iterdir() if p.is_file()}

    def assert_invalid(self, text=None):
        with self.assertRaises(V.VerificationError) as caught:
            V.verify_bundle(self.bundle)
        if text:
            self.assertIn(text, str(caught.exception))

    def cli(self, *args):
        return subprocess.run([sys.executable, "-B", str(ROOT / "tools/verify_preview.py"), *map(str, args)],
                              capture_output=True, text=True)

    def test_success_read_only_and_expectations(self):
        before = self.snapshot()
        self.assertEqual(V.verify_bundle(self.bundle, COMMIT, VERSION), self.manifest)
        result = self.cli(self.bundle, "--expected-commit", COMMIT, "--expected-version", VERSION)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("VERIFIED:", result.stdout)
        self.assertIn("Not publisher authentication", result.stdout)
        self.assertEqual(result.stderr, "")
        self.assertEqual(before, self.snapshot())

    def test_optional_host(self):
        self.manifest["host"] = "linux-x86_64"
        self.manifest["host_capabilities"] = ["capabilities=1", "Compiler=1", "TFT=0", "LVGL=0"]
        self.add_artifact("-linux-x64.tar.gz")
        self.write_metadata()
        self.assertEqual(len(V.verify_bundle(self.bundle)["artifacts"]), 3)
        self.manifest["host_capabilities"] = None
        self.write_metadata()
        self.assert_invalid("capabilities")

    def test_expected_mismatches_and_usage(self):
        for options in (("--expected-commit", "c" * 40), ("--expected-version", "2.0.0-preview.2")):
            result = self.cli(self.bundle, *options)
            self.assertEqual(result.returncode, 1)
            self.assertIn("expected", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertEqual(result.stdout, "")
        for options in ((), (str(self.bundle), "--expected-commit", "abc"),
                        (str(self.bundle), "--expected-version", "2.0.0"), ("--unknown",)):
            self.assertEqual(self.cli(*options).returncode, 2)
        self.assertEqual(self.cli("--help").returncode, 0)

    def test_changed_bytes_and_size(self):
        name = next(iter(self.manifest["artifacts"]))
        path = self.bundle / name
        data = path.read_bytes()
        path.write_bytes(b"X" + data[1:])
        self.assert_invalid("digest mismatch")
        path.write_bytes(data + b"ordinary appended text")
        before = self.snapshot()
        self.assert_invalid("byte size mismatch")
        self.assertEqual(before, self.snapshot())

    def test_manifest_and_checksum_disagreement(self):
        path = self.bundle / "manifest.json"
        path.write_bytes(path.read_bytes() + b" ")
        self.assert_invalid("manifest.json: SHA256SUMS digest mismatch")
        self.write_metadata()
        name = next(iter(self.manifest["artifacts"]))
        sums = self.bundle / "SHA256SUMS"
        sums.write_text(sums.read_text().replace(self.manifest["artifacts"][name]["sha256"], "0" * 64))
        self.assert_invalid("manifest/SHA256SUMS")

    def test_missing_and_extra_files(self):
        for name in ["manifest.json", "SHA256SUMS", *self.manifest["artifacts"]]:
            with self.subTest(name=name):
                path = self.bundle / name
                data = path.read_bytes()
                path.unlink()
                self.assertEqual(self.cli(self.bundle).returncode, 1)
                path.write_bytes(data)
        extra = self.bundle / "extra.txt"
        extra.write_text("ordinary extra file")
        self.assert_invalid("unexpected entries")
        extra.unlink()
        extra.mkdir()
        self.assert_invalid("unexpected entries")
        self.assertEqual(self.cli(Path(self.temp.name)).returncode, 1)
        self.assertEqual(self.cli(Path(self.temp.name) / "absent").returncode, 1)
        self.assertEqual(self.cli(self.bundle / "manifest.json").returncode, 1)

    def test_checksum_exact_coverage_and_duplicates(self):
        path = self.bundle / "SHA256SUMS"
        original = path.read_text()
        lines = original.splitlines(keepends=True)
        for changed in ["".join(lines[1:]), original + lines[0], original + "0" * 64 + "  extra.zip\n",
                        original + "0" * 64 + "  SHA256SUMS\n", original + "\n",
                        original.replace("  ", " ", 1), original.replace(lines[0][:64], "z" * 64, 1)]:
            with self.subTest(changed=changed[:80]):
                path.write_text(changed)
                self.assert_invalid()
        path.write_text(original.replace("\n", "\r\n"))
        V.verify_bundle(self.bundle)

    def test_manifest_strict_schema(self):
        original = copy.deepcopy(self.manifest)
        changes = [("version", 2), ("version", True), ("format", "other"), ("channel", "stable"),
                   ("package_version", "2.0.0"), ("runtime_version", "2.0.0-preview.2"),
                   ("source_commit", "abc"), ("source_tree", "G" * 40), ("source_epoch", True),
                   ("firmware_included", 0), ("host", "other"), ("host_capabilities", []),
                   ("packaging_environment", {}), ("security", None), ("artifacts", [])]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                self.manifest = copy.deepcopy(original)
                self.manifest[key] = value
                (self.bundle / "manifest.json").write_text(json.dumps(self.manifest))
                self.assert_invalid()
        for missing in (True, False):
            self.manifest = copy.deepcopy(original)
            if missing:
                del self.manifest["publication"]
            else:
                self.manifest["unsupported"] = "field"
            self.write_metadata()
            self.assert_invalid("schema fields")

    def test_artifact_schema_and_inventory(self):
        original = copy.deepcopy(self.manifest)
        name = next(iter(original["artifacts"]))
        for field, value in [("bytes", True), ("bytes", -1), ("bytes", "42"), ("sha256", 0),
                             ("sha256", "A" * 64), ("sha256", "abc"), ("extra", 1)]:
            with self.subTest(field=field, value=value):
                self.manifest = copy.deepcopy(original)
                self.manifest["artifacts"][name][field] = value
                self.write_metadata()
                self.assert_invalid()
        self.manifest = copy.deepcopy(original)
        del self.manifest["artifacts"][name]["bytes"]
        self.write_metadata()
        self.assert_invalid()
        for changed in [self.stem + "-other.zip", name.replace(COMMIT[:12], "c" * 12),
                        name.replace(VERSION, "2.0.0-preview.2")]:
            self.manifest = copy.deepcopy(original)
            self.manifest["artifacts"][changed] = self.manifest["artifacts"].pop(name)
            (self.bundle / "manifest.json").write_text(json.dumps(self.manifest))
            self.assert_invalid("artifact names/coverage")

    def test_unsafe_references(self):
        # Filename strings only; no outside files, archive members or executable data.
        names = ["../source.zip", "/source.zip", "sub/source.zip", "sub\\source.zip",
                 ".", "..", "C:source.zip", "source zip", "source\tzip"]
        for name in names:
            with self.subTest(name=name):
                manifest = copy.deepcopy(self.manifest)
                manifest["artifacts"] = {name: {"bytes": 1, "sha256": "0" * 64}}
                with self.assertRaisesRegex(V.VerificationError, "unsafe filename"):
                    V.parse_manifest(json.dumps(manifest).encode())
                with self.assertRaisesRegex(V.VerificationError, "unsafe filename"):
                    V.parse_checksums(("0" * 64 + "  " + name + "\n").encode())

    def test_malformed_metadata_and_duplicate_json(self):
        name = next(iter(self.manifest["artifacts"]))
        valid = json.dumps(self.manifest)
        duplicate_artifact = valid.replace('"artifacts": {', '"artifacts": {"' + name + '": {}, ', 1)
        samples = [b"\xff", b"{", b"[]", (valid[:-1] + ', "version": 1}').encode(), duplicate_artifact.encode(),
                   valid.replace("1700000000", "NaN").encode(),
                   valid.replace("1700000000", "Infinity").encode(),
                   valid.replace("1700000000", "-Infinity").encode(),
                   ("[" * 1100 + "0" + "]" * 1100).encode(),
                   valid.replace("1700000000", "1" * 5000).encode()]
        for data in samples:
            with self.subTest(data=data[:80]):
                (self.bundle / "manifest.json").write_bytes(data)
                result = self.cli(self.bundle)
                self.assertEqual(result.returncode, 1)
                self.assertIn("ERROR:", result.stderr)
                self.assertNotIn("Traceback", result.stderr)
        self.write_metadata()
        (self.bundle / "SHA256SUMS").write_bytes(b"\xff")
        self.assert_invalid("UTF-8")

    def test_nonregular_and_symlink_inputs(self):
        for name in ["manifest.json", "SHA256SUMS", *self.manifest["artifacts"]]:
            with self.subTest(name=name):
                path = self.bundle / name
                data = path.read_bytes()
                outside = Path(self.temp.name) / "ordinary-data"
                outside.write_bytes(data)
                path.unlink()
                path.symlink_to(outside)
                self.assert_invalid("regular non-symlink")
                path.unlink()
                path.mkdir()
                self.assert_invalid("regular non-symlink")
                path.rmdir()
                path.write_bytes(data)
        link = Path(self.temp.name) / "bundle-link"
        link.symlink_to(self.bundle, target_is_directory=True)
        with self.assertRaisesRegex(V.VerificationError, "not a symlink"):
            V.verify_bundle(link)
        if hasattr(os, "mkfifo"):
            for name in ["manifest.json", "SHA256SUMS", *self.manifest["artifacts"]]:
                with self.subTest(fifo=name):
                    path = self.bundle / name
                    data = path.read_bytes()
                    path.unlink()
                    os.mkfifo(path)
                    self.assert_invalid("regular non-symlink")
                    path.unlink()
                    path.write_bytes(data)

    def test_streaming_and_metadata_limit(self):
        name = next(iter(self.manifest["artifacts"]))
        data = b"ordinary fixture bytes\n" * 100000
        (self.bundle / name).write_bytes(data)
        self.manifest["artifacts"][name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        self.write_metadata()
        before = self.snapshot()
        V.verify_bundle(self.bundle)
        self.assertEqual(before, self.snapshot())
        (self.bundle / "manifest.json").write_bytes(b" " * (V.METADATA_LIMIT + 1))
        self.assert_invalid("1 MiB")

    def test_actual_packager_output(self):
        import package_preview as P
        repo = Path(self.temp.name) / "repo"
        repo.mkdir()
        for name in ["include/mcs.h", "docs/V2_PREVIEW.md", "LICENSE", "SECURITY.md", "library.json", "CMakeLists.txt"]:
            target = repo / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        studio = repo / "tools/studio"
        studio.mkdir(parents=True)
        for name in ["index.html", "studio.css", "studio.js", "api.js", "docs.js", "lang.js", "project.js", "source.js", "templates.js", "snippets.js"]:
            shutil.copy2(ROOT / "tools/studio" / name, studio / name)
        def git(*args):
            return subprocess.check_output(["git", "-C", str(repo), *args])
        git("init", "-q")
        git("add", ".")
        git("-c", "user.name=Verification Test", "-c", "user.email=verify@example.invalid", "commit", "-qm", "own ordinary package fixture")
        with mock.patch.object(P, "ROOT", repo):
            bundle = P.package(VERSION, Path(self.temp.name) / "packages")
        before = {p.name: p.read_bytes() for p in bundle.iterdir()}
        manifest = V.verify_bundle(bundle, git("rev-parse", "HEAD").decode().strip(), VERSION)
        self.assertEqual(manifest["source_tree"], git("rev-parse", "HEAD^{tree}").decode().strip())
        self.assertEqual(self.cli(bundle).returncode, 0)
        self.assertEqual(before, {p.name: p.read_bytes() for p in bundle.iterdir()})


if __name__ == "__main__":
    unittest.main(verbosity=2)
