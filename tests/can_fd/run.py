#!/usr/bin/env python3
"""Build/run common CAN FD contracts using only trusted compiler-generated images."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VARIANTS = {
    "default": [], "gc-stress": ["-DMCS_GC_STRESS=1"],
    "compiler-off": ["-DMCS_ENABLE_COMPILER=0"], "xip-off": ["-DMCS_ENABLE_XIP=0"],
    "compact": ["-DMCS_COMPACT_VALUES=1"], "int64": ["-DMCS_INT64=1"],
    "min": ["-DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN"],
    "lowram": ["-DMCS_DEFAULT_PROFILE=MCS_PROFILE_LOWRAM"],
    "min-hal": ["-DMCS_DEFAULT_PROFILE=MCS_PROFILE_MIN", "-DMCS_ENABLE_HAL=1"],
    "queue-off": ["-DMCS_HAL_SIM_CAN_FD_QUEUE=0"],
    "hal-off": ["-DMCS_ENABLE_HAL=0"],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--variant", action="append", choices=VARIANTS)
    parser.add_argument("--build-dir", type=Path, default=None)
    args = parser.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = (args.build_dir or ROOT / "build/can-fd-tests" / stamp).resolve()
    # Every run is immutable evidence; avoid hiding a previous failed run.
    out.mkdir(parents=True, exist_ok=False)
    fixtures = out / "fixtures"
    fixtures.mkdir()
    sources = sorted(ROOT.glob("src/*.c")) + sorted(ROOT.glob("modules/*/*.c"))
    test = ROOT / "tests/can_fd/test_can_fd.c"
    command = shlex.split(args.cc) + ["-std=gnu99", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                                    "-I" + str(ROOT / "include"), "-DMCS_USER_CONFIG=0"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    selected = args.variant or list(VARIANTS)
    record = {
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "tree": subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip(),
        "status": subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True),
        "compiler": subprocess.check_output(shlex.split(args.cc) + ["--version"], text=True).splitlines()[0],
        "sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in sources + sorted((ROOT / "include").rglob("*.h")) +
                   [ROOT / "include/mcs_capabilities.def", test, Path(__file__)]},
        "variants": {name: VARIANTS[name] for name in selected}, "commands": [], "completed": [],
        "test_vm_heap_limit": 262144, "test_vm_stack_slots": 512,
        "scope": "Host software/native tests; trusted compiler images only; no physical CAN bus or minimum-device-RAM claim",
    }

    def save():
        (out / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")

    def run(cmd, name):
        record["commands"].append(cmd)
        save()
        with (out / name).open("x") as log:
            subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=300)

    def build(name):
        binary = out / name
        print("BUILD", name, flush=True)
        run(command + VARIANTS[name] + list(map(str, sources)) + [str(test), "-lm", "-o", str(binary)], name + "-build.log")
        return binary

    default = build("default")
    run([str(default), "--emit", str(fixtures)], "emit.log")
    record["fixture_sha256"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in fixtures.glob("*.mcsb")}
    for name in selected:
        binary = default if name == "default" else build(name)
        run([str(binary), str(fixtures)], name + ".log")
        print(name, (out / (name + ".log")).read_text().strip(), flush=True)
        record["completed"].append(name)
        save()
    print("PASS", len(selected), "CAN FD variants; physical backend validation and full v2 remain separate", flush=True)


if __name__ == "__main__":
    main()
