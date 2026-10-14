#!/usr/bin/env python3
"""Policy/archive unit fixtures, or actual extracted candidate smoke via --bundle."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_preview as P
from release_policy import validate
VERSION = "2.0.0-preview.1"


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        for name in ["include/mcs.h", "docs/V2_PREVIEW.md", "LICENSE", "SECURITY.md", "library.json", "CMakeLists.txt"]:
            p = self.root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, p)
        (self.root / ".gitignore").write_text("build/\n__pycache__/\n")
        studio = self.root / "tools/studio"
        studio.mkdir(parents=True)
        for name in ["index.html", "studio.css", "studio.js", "api.js", "docs.js", "lang.js", "project.js", "source.js", "templates.js", "snippets.js"]:
            shutil.copy2(ROOT / "tools/studio" / name, studio / name)
        self.run_git("init", "-q")
        self.run_git("add", ".")
        self.run_git("-c", "user.name=Preview Test", "-c", "user.email=preview@example.invalid", "commit", "-qm", "trusted package fixture")
        self.original = P.ROOT
        P.ROOT = self.root

    def tearDown(self):
        P.ROOT = self.original
        self.temp.cleanup()

    def run_git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args])

    def package(self, out="out"):
        return P.package(VERSION, Path(self.temp.name) / out)

    def test_stable_policy(self):
        self.assertEqual(validate("stable", "1.14.0", "refs/heads/main"), "1.14.0")
        for ref in ["refs/heads/feat/microcs-v2", "refs/tags/v1.14.0", None]:
            with self.subTest(ref=ref), self.assertRaises(ValueError):
                validate("stable", "1.14.0", ref)
        for version in [VERSION, "2.0", "v2.0.0", "2.0.0\n", "02.0.0"]:
            with self.subTest(version=version), self.assertRaises(ValueError):
                validate("stable", version, "refs/heads/main")

    def test_preview_policy(self):
        self.assertEqual(validate("preview", VERSION), VERSION)
        for version in ["2.0.0", "1.14.0", "2.0.0-preview.0", "2.0.0-preview.01", "2.0.0-preview.1\n", "../../latest", "2.0.0-rc.1"]:
            with self.subTest(version=version), self.assertRaises(ValueError):
                validate("preview", version)
        with self.assertRaises(ValueError):
            validate("unknown", VERSION)

    def test_clean_and_tracked_only(self):
        (self.root / "build").mkdir()
        (self.root / "build/private-local.txt").write_text("test-only local sentinel")
        bundle = self.package()
        m = json.loads((bundle / "manifest.json").read_text())
        self.assertEqual(m["runtime_version"], VERSION)
        self.assertEqual(m["source_commit"], self.run_git("rev-parse", "HEAD").decode().strip())
        self.assertFalse(m["firmware_included"])
        source = next(bundle.glob("*-source.tar.gz"))
        with tarfile.open(source) as archive:
            self.assertFalse(any("private-local" in p.name or "/.git/" in p.name for p in archive))
        with zipfile.ZipFile(next(bundle.glob("*-studio.zip"))) as archive:
            names = archive.namelist()
            for name in ["index.html", "source.js", "project.js", "templates.js", "LICENSE", "V2_PREVIEW.md"]:
                self.assertTrue(any(p.endswith("/" + name) for p in names))
        for name, value in m["artifacts"].items():
            self.assertEqual(value["sha256"], hashlib.sha256((bundle / name).read_bytes()).hexdigest())
        with self.assertRaises(ValueError):
            self.package()

    def test_dirty_rejected(self):
        (self.root / "README-local.txt").write_text("untracked work")
        with self.assertRaises(ValueError):
            self.package()
        (self.root / "README-local.txt").unlink()
        with (self.root / "include/mcs.h").open("a") as f:
            f.write("\n/* uncommitted */\n")
        with self.assertRaises(ValueError):
            self.package()

    def test_runtime_label_and_modules(self):
        with self.assertRaises(ValueError):
            P.package("2.0.0-preview.2", Path(self.temp.name) / "out")
        self.run_git("rm", "tools/studio/source.js")
        self.run_git("-c", "user.name=Preview Test", "-c", "user.email=preview@example.invalid", "commit", "-qm", "missing module fixture")
        with self.assertRaises(ValueError):
            self.package()

    def test_library_metadata_rejected(self):
        path = self.root / "library.json"
        value = json.loads(path.read_text())
        value["version"] = "1.14.0"
        path.write_text(json.dumps(value))
        self.run_git("add", "library.json")
        self.run_git("-c", "user.name=Preview Test", "-c", "user.email=preview@example.invalid", "commit", "-qm", "mismatched library fixture")
        with self.assertRaises(ValueError):
            self.package()

    def test_repeatable_archives(self):
        first, second = self.package("one"), self.package("two")
        self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()}, {p.name: p.read_bytes() for p in second.iterdir()})

    def test_invalid_host_rejected_before_output(self):
        host = Path(self.temp.name) / "not-a-host"
        host.write_text("fixture, not an executable")
        with self.assertRaises(ValueError):
            P.package(VERSION, Path(self.temp.name) / "out", host)
        self.assertFalse((Path(self.temp.name) / "out").exists())
        host.write_bytes(b"\x7fELF\x02\x01")
        with self.assertRaises(ValueError):
            P.package(VERSION, Path(self.temp.name) / "out", host)



def verify_bundle(bundle):
    bundle = bundle.resolve()
    manifest = json.loads((bundle / "manifest.json").read_text())
    validate("preview", manifest["package_version"])
    assert manifest["runtime_version"] == manifest["package_version"]
    # These are freshly created OWN candidate archives, not arbitrary downloaded images.
    for line in (bundle / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split("  ", 1)
        assert Path(name).name == name and name not in [".", ".."]
        assert hashlib.sha256((bundle / name).read_bytes()).hexdigest() == digest
    smoke = ROOT / "build/preview-package-smoke"
    if smoke.exists():
        shutil.rmtree(smoke)
    smoke.mkdir(parents=True)
    with zipfile.ZipFile(next(bundle.glob("*-studio.zip"))) as archive:
        archive.extractall(smoke)
    with tarfile.open(next(bundle.glob("*-source.tar.gz"))) as archive:
        archive.extractall(smoke, filter="data")
    if manifest["host"]:
        with tarfile.open(next(bundle.glob("*-linux-x64.tar.gz"))) as archive:
            archive.extractall(smoke, filter="data")
        binary = next(smoke.glob("*-linux-x64/mcs"))
        assert subprocess.check_output([str(binary), "--version"], timeout=5).decode().strip() == "MicroCS " + manifest["runtime_version"]
        source = smoke / "valid-preview-smoke.cs"
        source.write_text('Console.WriteLine(40+2);\nConsole.WriteLine("فارسی 🚀");\n')
        result = subprocess.run([str(binary), str(source)], capture_output=True, timeout=5)
        assert result.returncode == 0 and result.stdout == '42\nفارسی 🚀\n'.encode(), result.stderr
    assert next(smoke.glob("*-source/include/mcs.h")).read_bytes() == (ROOT / "include/mcs.h").read_bytes()
    for name in ["index.html", "source.js", "project.js", "templates.js"]:
        assert next(smoke.glob("*-studio/" + name)).is_file()
    print("PASS candidate SHA256SUMS, source/header identity, extracted host version/runtime Unicode and offline module completeness")
    print(smoke)


if __name__ == "__main__":
    if "--bundle" in sys.argv:
        parser = argparse.ArgumentParser()
        parser.add_argument("--bundle", type=Path, required=True)
        verify_bundle(parser.parse_args().bundle)
    else:
        unittest.main(verbosity=2)
