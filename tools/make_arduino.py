#!/usr/bin/env python3
"""Build the Arduino IDE library: dist/arduino/MicroCS (and MicroCS-<version>.zip).

Arduino libraries keep every source + header under src/, so this flattens
include/, src/, modules/*/ and ports/arduino/ into one folder and copies the
.ino examples. Install the zip with Sketch > Include Library > Add .ZIP Library.

    python3 tools/make_arduino.py [--out dist/arduino]
"""
import argparse
import glob
import os
import re
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def version():
    with open(os.path.join(ROOT, "include", "mcs.h")) as f:
        m = re.search(r'#define MCS_VERSION_STRING "([^"]+)"', f.read())
    return m.group(1) if m else "0.0.0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "arduino"))
    a = ap.parse_args()
    lib = os.path.join(a.out, "MicroCS")
    shutil.rmtree(lib, ignore_errors=True)
    src = os.path.join(lib, "src")
    os.makedirs(os.path.join(src, "profiles"))
    files = (glob.glob(os.path.join(ROOT, "include", "*.h")) + glob.glob(os.path.join(ROOT, "src", "*.[ch]"))
             + glob.glob(os.path.join(ROOT, "modules", "*", "*.c"))
             + glob.glob(os.path.join(ROOT, "ports", "arduino", "*.[ch]*")))
    for f in files:
        if os.path.basename(f) == "mcs_vfs_posix.c":
            continue                     # host only
        shutil.copy(f, src)
    for f in glob.glob(os.path.join(ROOT, "include", "profiles", "*.h")):
        shutil.copy(f, os.path.join(src, "profiles"))
    for d in glob.glob(os.path.join(ROOT, "ports", "arduino", "examples", "*")):
        shutil.copytree(d, os.path.join(lib, "examples", os.path.basename(d)))
    shutil.copy(os.path.join(ROOT, "LICENSE"), lib)
    v = version()
    with open(os.path.join(lib, "library.properties"), "w") as f:
        f.write(f"""name=MicroCS
version={v}
author=MicroCS contributors
maintainer=amir1387aht
sentence=Run C# on your board: REPL, scripts, GPIO, UART, I2C, SPI, ADC, PWM, timers.
paragraph=A compact C# compiler + VM for 32-bit boards (ESP32, RP2040, SAMD51, nRF52, STM32, Teensy). Use it as an interactive REPL firmware or embed scripts in your sketch.
category=Other
url=https://github.com/amir1387aht/MicroCS
architectures=*
includes=MicroCS.h
""")
    zpath = os.path.join(a.out, f"MicroCS-{v}.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for base, _, names in os.walk(lib):
            for n in names:
                p = os.path.join(base, n)
                z.write(p, os.path.relpath(p, a.out))
    print(f"{lib}\n{zpath}")


if __name__ == "__main__":
    main()
