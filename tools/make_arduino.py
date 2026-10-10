#!/usr/bin/env python3
"""Build the Arduino IDE library: dist/arduino/MicroCS (and MicroCS-<version>.zip).

Arduino libraries keep every source + header under src/, so this flattens
include/, src/, modules/*/ and ports/arduino/ into one folder and copies the
.ino examples. Install the zip with Sketch > Include Library > Add .ZIP Library.

    python3 tools/make_arduino.py [--out dist/arduino]

Configuration: Arduino libraries cannot take -D flags, so the library ships the
project config header src/mcs_user_config.h (config/mcs_user_config.h: every
MCS_* option with its default value; with --profile, the values of that
profile, written by tools/gen_config.py). Edit it in the installed
library (Arduino/libraries/MicroCS/src/), or bake the options in here:

    --config FILE           start from your own mcs_user_config.h instead of the template
    --profile NAME          full | auto | embedded | mcu | lowram | tiny | min | linux
    --no-ws2812             leave out the "ws2812" driver (C# LedStrip)
    --no-servo              leave out the "servo" driver (C# Servo)
    --u8g2                  C# U8g2 / U8x8 displays (docs/U8G2.md): needs the U8g2 library
                            (Library Manager: "U8g2"), which then is a dependency
    --u8g2-displays A,B     displays compiled in (python3 tools/u8g2.py displays)
    --u8g2-fonts A,B        U8g2 fonts in flash, the first is the default (tools/u8g2.py fonts)
    --u8x8-fonts A,B        U8x8 fonts in flash
    --define NAME[=VALUE]   any MCS_* option, e.g. --define MCS_ENABLE_SCHED=0 (repeatable)
    --fs littlefs|yaffs2|tinyfs  bundle a flash filesystem so mcs_flashfs_mount() works on any
                            mcs_flash_t (SPI NOR / NAND chip, internal flash). The board
                            cores' own LittleFS (ESP32, RP2040) needs none of this.
    --fs-dir DIR            its sources (littlefs: lfs.c + lfs.h; yaffs2: the checkout
                            with direct/ and core/). Default: $MICROCS_LITTLEFS_DIR /
                            $MICROCS_YAFFS2_DIR, ../littlefs or ../yaffs2 next to MicroCS,
                            third_party/, build/third_party (make fetch-lfs / fetch-yaffs).
                            Nothing is downloaded. YAFFS2 is GPLv2 (or commercial).
"""
import argparse
import glob
import os
import re
import shutil
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LFS_VERSION = "2.9.3"
YAFFS_REV = "474b3acb927d27b2305618aaf24456b9d33fe91b"
YAFFS_CORE = ("yaffs_ecc yaffs_cache yaffs_guts yaffs_tagscompat yaffs_tagsmarshall yaffs_packedtags1 "
              "yaffs_packedtags2 yaffs_nand yaffs_checkptrw yaffs_nameval yaffs_allocator yaffs_yaffs1 "
              "yaffs_yaffs2 yaffs_bitmap yaffs_endian yaffs_verify yaffs_summary").split()
YAFFS_DIRECT = ["yaffsfs", "yaffs_attribs", "yaffs_error", "yaffs_hweight"]
YAFFS_DEFS = ["CONFIG_YAFFS_DIRECT", "CONFIG_YAFFS_YAFFS2", "CONFIG_YAFFS_DEFINES_TYPES",
              "CONFIG_YAFFS_PROVIDE_DEFS", "CONFIG_YAFFSFS_PROVIDE_VALUES", "Y_LOFF_T=off_t"]


def find_fs(fs, given):
    """Folder with the filesystem sources (never downloads)."""
    markers = ["lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"] if fs == "littlefs" else \
              ["direct/yaffsfs.c", "core/yaffs_guts.c"]
    env = os.environ.get("MICROCS_LITTLEFS_DIR" if fs == "littlefs" else "MICROCS_YAFFS2_DIR")
    tag = LFS_VERSION if fs == "littlefs" else YAFFS_REV
    cands = [given] if given else ([env] if env else []) + [
        os.path.join(ROOT, "..", fs), os.path.join(ROOT, "third_party", fs),
        os.path.join(ROOT, "build", "third_party", f"{fs}-{tag}")]
    for c in cands:
        if c and all(os.path.exists(os.path.join(c, m)) for m in markers):
            return os.path.abspath(c)
    where = given or env or "../%s, third_party/%s, build/third_party" % (fs, fs)
    raise SystemExit(f"make_arduino: no {fs} sources ({where}). Pass --fs-dir DIR "
                     f"(git clone https://github.com/{'littlefs-project/littlefs -b v' + LFS_VERSION if fs == 'littlefs' else 'Aleph-One-Ltd/yaffs2'}) "
                     f"or run `make fetch-{'lfs' if fs == 'littlefs' else 'yaffs'}` first.")


def bundle_fs(fs, d, src):
    """Copy the filesystem into the flat src/ folder; returns the config defines."""
    if fs == "tinyfs":
        return ["MCS_ENABLE_TINYFS=1"]                      # part of MicroCS: nothing to copy
    if fs == "littlefs":
        for f in ("lfs.c", "lfs.h", "lfs_util.c", "lfs_util.h"):
            shutil.copy(os.path.join(d, f), src)
        with open(os.path.join(src, "lfs_util.h")) as f:
            txt = f.read()
        with open(os.path.join(src, "lfs_util.h"), "w") as f:   # quiet, like -DLFS_NO_* in CMake
            f.write("/* MicroCS Arduino bundle */\n#ifndef LFS_NO_DEBUG\n#define LFS_NO_DEBUG\n#endif\n"
                    "#ifndef LFS_NO_WARN\n#define LFS_NO_WARN\n#endif\n#ifndef LFS_NO_ERROR\n#define LFS_NO_ERROR\n#endif\n" + txt)
        return ["MCS_ENABLE_LFS=1"]
    prelude = "/* MicroCS Arduino bundle */\n#include <sys/types.h>\n#include \"mcs_yaffs_config.h\"\n"
    for f in glob.glob(os.path.join(d, "direct", "*.h")):
        shutil.copy(f, src)
    for f in YAFFS_DIRECT:
        with open(os.path.join(d, "direct", f + ".c")) as fi:
            txt = fi.read()
        with open(os.path.join(src, f + ".c"), "w") as fo:
            fo.write(prelude + txt)
    for f in YAFFS_CORE + ["yaffs_getblockinfo", "yaffs_trace", "yaffs_attribs"]:
        for e in ("c", "h"):
            p = os.path.join(d, "core", f"{f}.{e}")
            if not os.path.exists(p) or (e == "c" and f not in YAFFS_CORE):
                continue
            with open(p) as fi:
                txt = fi.read()
            if f in YAFFS_CORE:
                for fn in ("strcat", "strcpy", "strncpy", "strnlen", "strcmp", "strncmp"):
                    txt = txt.replace(fn, "yaffs_" + fn)
            txt = txt.replace("loff_t", "Y_LOFF_T")
            with open(os.path.join(src, f"{f}.{e}"), "w") as fo:
                fo.write((prelude if e == "c" else "") + txt)
    with open(os.path.join(src, "mcs_yaffs_config.h"), "w") as f:
        f.write("/* MicroCS Arduino bundle: the yaffs2 build options (-D flags elsewhere) */\n#pragma once\n")
        for dd in YAFFS_DEFS:
            n, _, v = dd.partition("=")
            f.write(f"#ifndef {n}\n#define {n} {v or 1}\n#endif\n")
    return ["MCS_ENABLE_YAFFS=1", "MCS_YAFFS_OSGLUE=1"]


PROFILES = ["full", "auto", "embedded", "mcu", "lowram", "tiny", "min", "linux"]


def version():
    with open(os.path.join(ROOT, "include", "mcs.h")) as f:
        m = re.search(r'#define MCS_VERSION_STRING "([^"]+)"', f.read())
    return m.group(1) if m else "0.0.0"


def u8g2_defines(displays, fonts, xfonts):
    """MCS_ENABLE_U8G2 + the X-macro lists (names checked when the u8g2 sources are around)"""
    split = lambda v: [x for x in (v or "").replace(" ", ",").split(",") if x]
    ds = split(displays)
    fs = ["u8g2_font_" + f[10:] if f.startswith("u8g2_font_") else "u8g2_font_" + f for f in split(fonts)]
    xs = ["u8x8_font_" + f[10:] if f.startswith("u8x8_font_") else "u8x8_font_" + f for f in split(xfonts)]
    out = ["MCS_ENABLE_U8G2=1"]
    if ds:
        out.append("MCS_U8G2_DISPLAYS=" + " ".join(f"U8G2_DISPLAY({d})" for d in ds))
    if fs:
        out.append("MCS_U8G2_FONTS=" + " ".join(f"U8G2_FONT({f})" for f in fs))
    if xs:
        out.append("MCS_U8X8_FONTS=" + " ".join(f"U8X8_FONT({f})" for f in xs))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "arduino"))
    ap.add_argument("--no-ws2812", action="store_true", help="leave out the ws2812 driver (C# LedStrip)")
    ap.add_argument("--no-servo", action="store_true", help="leave out the servo driver (C# Servo)")
    ap.add_argument("--u8g2", action="store_true", help="C# U8g2 / U8x8 (needs the U8g2 library)")
    ap.add_argument("--u8g2-displays", help="comma list of u8g2 displays to compile in")
    ap.add_argument("--u8g2-fonts", help="comma list of U8g2 fonts in flash")
    ap.add_argument("--u8x8-fonts", help="comma list of U8x8 fonts in flash")
    ap.add_argument("--define", action="append", default=[], metavar="NAME[=VALUE]", help="MCS_* build option")
    ap.add_argument("--config", help="your mcs_user_config.h (default: config/mcs_user_config.h)")
    ap.add_argument("--profile", choices=PROFILES, help="build profile (MCS_PROFILE)")
    ap.add_argument("--fs", choices=["littlefs", "yaffs2", "tinyfs"], help="bundle a flash filesystem")
    ap.add_argument("--fs-dir", help="filesystem sources (default: looked up, never downloaded)")
    a = ap.parse_args()
    fs_dir = find_fs(a.fs, a.fs_dir) if a.fs and a.fs != "tinyfs" else None
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
    defines = list(a.define) + (["MCS_ENABLE_WS2812=0"] if a.no_ws2812 else []) + (["MCS_ENABLE_SERVO=0"] if a.no_servo else [])
    if a.profile:
        defines.append("MCS_PROFILE=MCS_PROFILE_" + a.profile.upper())
    if a.u8g2 or a.u8g2_displays or a.u8g2_fonts or a.u8x8_fonts:
        a.u8g2 = True
        defines += u8g2_defines(a.u8g2_displays, a.u8g2_fonts, a.u8x8_fonts)
    if a.fs:
        defines += bundle_fs(a.fs, fs_dir, src)
        print(f"bundled {a.fs}" + (f" from {fs_dir}" if fs_dir else " (built in)"))
    # Arduino has no -D for libraries: the options go into the library's mcs_user_config.h,
    # which mcs_config.h finds next to itself (library and sketch see the same file)
    config = a.config or os.path.join(ROOT, "config", "mcs_user_config.h")
    if not os.path.isfile(config):
        raise SystemExit(f"make_arduino: no config header {config}")
    if a.config or a.profile in (None, "full"):
        with open(config) as f:
            txt = f.read()
    elif a.profile == "auto":
        # the board's RAM/flash is only known when the sketch is compiled
        txt = ("#ifndef MCS_USER_CONFIG_H\n#define MCS_USER_CONFIG_H\n"
               "/* MCS_PROFILE_AUTO: the defaults follow the board's RAM and flash.\n"
               " * Every option (with its full-profile default) is listed in MicroCS's\n"
               " * config/mcs_user_config.h; add the ones you change here. */\n"
               "#endif /* MCS_USER_CONFIG_H */\n")
    else:
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import gen_config
        txt = gen_config.generate(a.profile, 0, 0, [])
    head = ""
    if defines or a.fs == "yaffs2":
        head = "/* make_arduino.py build options (like -D: they win over the settings below) */\n"
        for dd in defines:
            n, _, v = dd.partition("=")
            if not re.match(r"^[A-Za-z_]\w*$", n):
                raise SystemExit(f"make_arduino: bad --define {dd}")
            head += f"#ifndef {n}\n#define {n} {v or 1}\n#endif\n"
        if a.fs == "yaffs2":
            head += '#include "mcs_yaffs_config.h"\n'
        head += "\n"
    with open(os.path.join(src, "mcs_user_config.h"), "w") as f:
        f.write(head + txt)
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
""" + ("depends=U8g2\n" if a.u8g2 else ""))
    zpath = os.path.join(a.out, f"MicroCS-{v}.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for base, _, names in os.walk(lib):
            for n in names:
                p = os.path.join(base, n)
                z.write(p, os.path.relpath(p, a.out))
    print(f"{lib}\n{zpath}")


if __name__ == "__main__":
    main()
