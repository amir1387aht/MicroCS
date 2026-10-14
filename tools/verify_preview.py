#!/usr/bin/env python3
"""Verify a local MicroCS preview bundle's byte and metadata consistency offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys

# This policy matches package_preview.py's current manifest and release_policy.py.
# New manifest versions/fields must be reviewed rather than silently accepted.
VERSION = re.compile(r"2\.0\.0-preview\.[1-9][0-9]*\Z")
COMMIT = re.compile(r"[0-9a-f]{40}\Z")
DIGEST = re.compile(r"[0-9a-f]{64}\Z")
FIELDS = {
    "format", "version", "channel", "package_version", "runtime_version",
    "source_commit", "source_tree", "source_epoch", "host", "firmware_included",
    "host_capabilities", "packaging_environment", "graphics", "security",
    "publication", "artifacts",
}
METADATA_LIMIT = 1024 * 1024


class VerificationError(ValueError):
    """Bundle cannot be verified under the supported packaging policy."""


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def safe_name(name):
    require(isinstance(name, str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name)
            is not None, "unsafe filename reference (only simple ASCII leaf names are supported)")
    return name


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "manifest contains a duplicate JSON key")
        result[key] = value
    return result


def invalid_constant(value):
    raise VerificationError("manifest.json: nonstandard JSON numeric constant")


def signature(info):
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def read_file(path, metadata=False):
    """Read/hash a regular non-symlink file; never write to or interpret its bytes."""
    before = path.lstat()
    require(stat.S_ISREG(before.st_mode), f"{path.name}: expected a regular non-symlink file")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    descriptor = os.open(path, flags)
    with os.fdopen(descriptor, "rb") as stream:
        opened = os.fstat(stream.fileno())
        require(stat.S_ISREG(opened.st_mode) and signature(before) == signature(opened),
                f"{path.name}: file changed while opening")
        if metadata:
            require(opened.st_size <= METADATA_LIMIT, f"{path.name}: metadata exceeds 1 MiB limit")
            data = stream.read(METADATA_LIMIT + 1)
            require(len(data) <= METADATA_LIMIT, f"{path.name}: metadata exceeds 1 MiB limit")
        else:
            digest, size = hashlib.sha256(), 0
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
                size += len(chunk)
            data = (size, digest.hexdigest())
        require(signature(opened) == signature(os.fstat(stream.fileno()))
                == signature(path.lstat()), f"{path.name}: file changed while reading")
    return data


def parse_manifest(data):
    try:
        manifest = json.loads(data.decode("utf-8"), object_pairs_hook=unique_object,
                              parse_constant=invalid_constant)
    except VerificationError:
        raise
    except (UnicodeError, ValueError, RecursionError) as error:
        raise VerificationError("manifest.json: invalid UTF-8 JSON") from error
    require(isinstance(manifest, dict), "manifest.json: expected an object")
    require(manifest.get("format") == "microcs-preview-manifest"
            and type(manifest.get("version")) is int and manifest["version"] == 1,
            "manifest.json: unsupported format/version (expected microcs-preview-manifest v1)")
    require(set(manifest) == FIELDS, "manifest.json: missing or unsupported schema fields")
    require(manifest["channel"] == "preview", "manifest.json: channel must be preview")
    version = manifest["package_version"]
    require(isinstance(version, str) and VERSION.fullmatch(version) is not None,
            "manifest.json: package_version must be 2.0.0-preview.N (N starts at 1)")
    require(manifest["runtime_version"] == version, "manifest.json: runtime/package version mismatch")
    for field in ("source_commit", "source_tree"):
        require(isinstance(manifest[field], str) and COMMIT.fullmatch(manifest[field]) is not None,
                f"manifest.json: {field} must be a full lowercase 40-hex Git identifier")
    require(type(manifest["source_epoch"]) is int, "manifest.json: source_epoch must be an integer")
    require(manifest["firmware_included"] is False, "manifest.json: firmware_included must be false")
    require(manifest["host"] in (None, "linux-x86_64"), "manifest.json: unsupported host")
    caps = manifest["host_capabilities"]
    if manifest["host"] is None:
        require(caps is None, "manifest.json: host_capabilities must be null without a host")
    else:
        require(isinstance(caps, list) and all(isinstance(line, str) for line in caps)
                and {"capabilities=1", "Compiler=1", "TFT=0", "LVGL=0"}.issubset(caps),
                "manifest.json: missing or invalid default-host capabilities")
    env = manifest["packaging_environment"]
    require(isinstance(env, dict) and set(env) == {"system", "machine", "libc", "python"},
            "manifest.json: unsupported packaging_environment schema")
    require(all(isinstance(env[key], str) for key in ("system", "machine", "python"))
            and isinstance(env["libc"], list) and len(env["libc"]) == 2
            and all(isinstance(value, str) for value in env["libc"]),
            "manifest.json: invalid packaging_environment values")
    for field in ("graphics", "security", "publication"):
        require(isinstance(manifest[field], str) and bool(manifest[field]),
                f"manifest.json: {field} must be nonempty text")
    artifacts = manifest["artifacts"]
    require(isinstance(artifacts, dict), "manifest.json: artifacts must be an object")
    for name, entry in artifacts.items():
        safe_name(name)
        require(isinstance(entry, dict) and set(entry) == {"bytes", "sha256"},
                f"{name}: invalid artifact schema")
        require(type(entry["bytes"]) is int and entry["bytes"] >= 0,
                f"{name}: bytes must be a nonnegative integer")
        require(isinstance(entry["sha256"], str) and DIGEST.fullmatch(entry["sha256"]) is not None,
                f"{name}: sha256 must be 64 lowercase hexadecimal characters")
    stem = f"MicroCS-{version}-g{manifest['source_commit'][:12]}"
    expected = {stem + "-source.tar.gz", stem + "-studio.zip"}
    if manifest["host"] is not None:
        expected.add(stem + "-linux-x64.tar.gz")
    require(set(artifacts) == expected, "manifest.json: artifact names/coverage do not match version, commit and host")
    return manifest


def parse_checksums(data):
    try:
        lines = data.decode("utf-8").splitlines()
    except UnicodeError as error:
        raise VerificationError("SHA256SUMS: invalid UTF-8") from error
    checks = {}
    for index, line in enumerate(lines, 1):
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        require(match is not None, f"SHA256SUMS: invalid checksum line {index} (expected SHA256, two spaces, filename)")
        digest, name = match.groups()
        safe_name(name)
        require(name not in checks, f"SHA256SUMS: duplicate filename {name}")
        checks[name] = digest
    return checks


def verify_bundle(bundle, expected_commit=None, expected_version=None):
    """Return validated manifest; check opaque archive bytes, not archive contents."""
    if expected_commit is not None:
        require(isinstance(expected_commit, str) and COMMIT.fullmatch(expected_commit) is not None,
                "expected commit must be a full lowercase 40-hex Git identifier")
    if expected_version is not None:
        require(isinstance(expected_version, str) and VERSION.fullmatch(expected_version) is not None,
                "expected version must be 2.0.0-preview.N (N starts at 1)")
    bundle = Path(bundle)
    require(stat.S_ISDIR(bundle.lstat().st_mode), "PATH must be a directory, not a symlink")
    manifest_data = read_file(bundle / "manifest.json", metadata=True)
    manifest = parse_manifest(manifest_data)
    checks_data = read_file(bundle / "SHA256SUMS", metadata=True)
    checks = parse_checksums(checks_data)
    expected = set(manifest["artifacts"]) | {"manifest.json"}
    require(set(checks) == expected, "SHA256SUMS: coverage must be exactly manifest.json and all listed archives")
    require({path.name for path in bundle.iterdir()} == expected | {"SHA256SUMS"},
            "bundle: missing or unexpected entries (use the single generated bundle directory)")
    require(hashlib.sha256(manifest_data).hexdigest() == checks["manifest.json"],
            "manifest.json: SHA256SUMS digest mismatch")
    if expected_commit is not None:
        require(manifest["source_commit"] == expected_commit, "manifest.json: expected commit mismatch")
    if expected_version is not None:
        require(manifest["package_version"] == expected_version, "manifest.json: expected version mismatch")
    for name, entry in sorted(manifest["artifacts"].items()):
        require(checks[name] == entry["sha256"], f"{name}: manifest/SHA256SUMS digest mismatch")
        size, digest = read_file(bundle / name)
        require(size == entry["bytes"], f"{name}: byte size mismatch")
        require(digest == entry["sha256"], f"{name}: SHA-256 digest mismatch")
    # Detect ordinary concurrent replacements of metadata/directory entries too.
    require(read_file(bundle / "manifest.json", metadata=True) == manifest_data
            and read_file(bundle / "SHA256SUMS", metadata=True) == checks_data
            and {path.name for path in bundle.iterdir()} == expected | {"SHA256SUMS"},
            "bundle changed during verification")
    return manifest


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Exit codes: 0 verified; 1 verification/I/O failure; 2 invalid usage. "
               "Checksums are not publisher authentication. Archive contents are not inspected or guaranteed safe.")
    parser.add_argument("path", type=Path, metavar="PATH", help="single generated bundle directory")
    parser.add_argument("--expected-commit", help="require a full lowercase 40-hex source commit")
    parser.add_argument("--expected-version", help="require a package version (2.0.0-preview.N)")
    args = parser.parse_args(argv)
    if args.expected_commit is not None and COMMIT.fullmatch(args.expected_commit) is None:
        parser.error("--expected-commit requires a full lowercase 40-hex Git identifier")
    if args.expected_version is not None and VERSION.fullmatch(args.expected_version) is None:
        parser.error("--expected-version requires 2.0.0-preview.N (N starts at 1)")
    try:
        manifest = verify_bundle(args.path, args.expected_commit, args.expected_version)
    except (VerificationError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"VERIFIED: {manifest['package_version']} commit {manifest['source_commit']} "
          f"({len(manifest['artifacts'])} archives; byte/metadata consistency only)")
    print("Not publisher authentication or a guarantee of archive-content safety. No archives extracted or executed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
