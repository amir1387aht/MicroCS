#!/usr/bin/env python3
"""Create clean-checkout v2 preview archives; never publish/tag/upload anything."""
import argparse
import datetime
import gzip
import hashlib
import io
import json
from pathlib import Path
import platform
import re
import struct
import subprocess
import tarfile
import tempfile
import zipfile
from release_policy import validate

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args])


def archive_tar(path, files, epoch):
    with path.open("wb") as out, gzip.GzipFile(filename="", fileobj=out, mode="wb", mtime=epoch) as gz, tarfile.open(fileobj=gz, mode="w|") as archive:
        for name, data, executable in sorted(files):
            info = tarfile.TarInfo(name)
            info.size, info.mtime = len(data), epoch
            info.mode = 0o755 if executable else 0o644
            archive.addfile(info, io.BytesIO(data))


def package(version, out, host_binary=None):
    validate("preview", version)
    if git("status", "--porcelain", "--untracked-files=all").strip():
        raise ValueError("Preview packaging requires a clean tracked/untracked checkout; commit intended changes first.")
    sha = git("rev-parse", "HEAD").decode().strip()
    tree = git("rev-parse", "HEAD^{tree}").decode().strip()
    epoch = int(git("show", "-s", "--format=%ct", "HEAD"))
    header = git("show", "HEAD:include/mcs.h").decode()
    match = re.search(r'^#define MCS_VERSION_STRING "([^"]+)"$', header, re.M)
    if not match:
        raise ValueError("Committed runtime header has no version label.")
    runtime = match.group(1)
    if runtime != version:
        raise ValueError("Package label and committed runtime version must match.")
    library = json.loads(git("show", "HEAD:library.json"))
    cmake = git("show", "HEAD:CMakeLists.txt").decode()
    if library.get("version") != runtime or f"project(MicroCS VERSION {runtime.split('-')[0]} LANGUAGES C)" not in cmake:
        raise ValueError("Source package library/CMake metadata does not match the preview version.")
    required = ["index.html", "studio.css", "studio.js", "api.js", "docs.js", "lang.js", "project.js", "source.js", "templates.js", "snippets.js"]
    tracked = [p for p in git("ls-files", "-z", "tools/studio").decode().split("\0") if p]
    names = {p.removeprefix("tools/studio/") for p in tracked}
    if not set(required).issubset(names):
        raise ValueError("Studio distribution is missing required offline modules.")
    host = None
    capabilities = None
    if host_binary:
        host_binary = Path(host_binary).resolve()
        host = host_binary.read_bytes()
        if len(host) < 64 or host[:5] != b"\x7fELF\x02" or host[5] != 1 or struct.unpack_from("<H", host, 18)[0] != 62:
            raise ValueError("The preview host bundle only supports Linux x86-64 ELF executables.")
        reported = subprocess.check_output([str(host_binary), "--version"], timeout=5).decode().strip()
        if reported != "MicroCS " + version:
            raise ValueError("Host executable version does not match the source/package label.")
        caps = subprocess.run([str(host_binary), "--shell", "--sim", "--ramfs", "65536"], input=b"caps\nexit\n", capture_output=True, timeout=5)
        capabilities = caps.stdout.decode().splitlines()
        if caps.returncode != 0 or not {"capabilities=1", "Compiler=1", "TFT=0", "LVGL=0"}.issubset(capabilities):
            raise ValueError("Preview host must report compiler enabled and optional TFT/LVGL OFF.")
    stem = f"MicroCS-{version}-g{sha[:12]}"
    out = Path(out).resolve()
    destination = out / stem
    if destination.exists():
        raise ValueError("Refusing to replace an existing preview package directory.")
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".preview-stage-", dir=out) as staging:
        stage = Path(staging)
        # Tracked source only; .git, build caches, local files and secrets never included.
        source = git("archive", "--format=tar", f"--prefix={stem}-source/", "HEAD")
        with (stage / f"{stem}-source.tar.gz").open("wb") as f, gzip.GzipFile(filename="", fileobj=f, mode="wb", mtime=epoch) as gz:
            gz.write(source)
        stamp = tuple(datetime.datetime.fromtimestamp(max(epoch, 315532800), datetime.timezone.utc).timetuple()[:6])
        readme = f"# MicroCS {version} preview candidate\n\nNot stable/full v2. No firmware is included. Use trusted source/compiler images\nonly; the shell is unauthenticated and this is not a sandbox.\n\n- Linux bundle: `./mcs --version`, then `./mcs --repl --sim`.\n- Studio bundle: open `index.html` in Chrome/Edge; editing works offline.\n- See [preview scope and limitations](V2_PREVIEW.md). Full build/API/security\n  documentation and referenced files are in the separate source archive.\n- Source commit: `{sha}`. Verify the separately supplied manifest/SHA256SUMS.\n  Checksums are not authentication. No tag or public release was created.\n"
        with zipfile.ZipFile(stage / f"{stem}-studio.zip", "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for relative in sorted(tracked):
                label = relative.removeprefix("tools/studio/")
                if label == "README.md":
                    label = "ENGINEERING-README.md"
                entry = zipfile.ZipInfo(f"{stem}-studio/" + label, stamp)
                entry.external_attr = 0o644 << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, git("show", "HEAD:" + relative))
            entry = zipfile.ZipInfo(f"{stem}-studio/README.md", stamp)
            entry.external_attr = 0o644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, readme)
            for name in ["LICENSE", "docs/V2_PREVIEW.md", "SECURITY.md"]:
                entry = zipfile.ZipInfo(f"{stem}-studio/" + Path(name).name, stamp)
                entry.external_attr = 0o644 << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, git("show", "HEAD:" + name))
        if host is not None:
            files = [(f"{stem}-linux-x64/mcs", host, True), (f"{stem}-linux-x64/README.md", readme.encode(), False),
                     (f"{stem}-linux-x64/V2_PREVIEW.md", git("show", "HEAD:docs/V2_PREVIEW.md"), False)]
            for name in ["LICENSE", "CHANGELOG.md", "SECURITY.md", "docs/SECURITY.md"]:
                files.append((f"{stem}-linux-x64/" + name, git("show", "HEAD:" + name), False))
            archive_tar(stage / f"{stem}-linux-x64.tar.gz", files, epoch)
        artifacts = {p.name: {"bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(stage.iterdir())}
        manifest = {"format": "microcs-preview-manifest", "version": 1, "channel": "preview", "package_version": version, "runtime_version": runtime,
                    "source_commit": sha, "source_tree": tree, "source_epoch": epoch, "host": "linux-x86_64" if host else None,
                    "firmware_included": False, "host_capabilities": capabilities,
                    "packaging_environment": {"system": platform.system(), "machine": platform.machine(), "libc": platform.libc_ver(), "python": platform.python_version()}, "graphics": "experimental opt-in source only; no graphics runtime in default host build",
                    "security": "trusted-source/compiler-generated images only; unauthenticated shell; no sandbox or signing claim",
                    "publication": "candidate artifacts only; exact-head CI/review required before publishing; no tag/release created",
                    "artifacts": artifacts}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        checks = [(hashlib.sha256(p.read_bytes()).hexdigest(), p.name) for p in sorted(stage.iterdir())]
        (stage / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for digest, name in checks))
        # Move only after every artifact/manifest has succeeded.
        stage.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", default="dist/preview")
    parser.add_argument("--host-binary", type=Path)
    args = parser.parse_args()
    try:
        print(package(args.version, args.out, args.host_binary))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
