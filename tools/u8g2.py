#!/usr/bin/env python3
"""u8g2 helper for MicroCS (docs/U8G2.md): find / fetch the library, list displays and
fonts, extract fonts as files for SetFont() and write the config lines.

    python3 tools/u8g2.py where                     where the u8g2 sources are (like the build finds them)
    python3 tools/u8g2.py fetch [--version 2.37.1]  download them into build/third_party/u8g2-<version>
    python3 tools/u8g2.py displays [FILTER]         display names for MCS_U8G2_DISPLAYS (bus, size, RAM)
    python3 tools/u8g2.py fonts [FILTER] [--u8x8]   font names with their size in bytes
    python3 tools/u8g2.py extract NAME... -o DIR    font files DIR/u8g2_font_NAME.bin (copy DIR to the
                                                    board's /fonts: SetFont("NAME") loads them)
    python3 tools/u8g2.py extract --all -o DIR      every font (2043 u8g2 + 131 u8x8 files, ~15 MB)
    python3 tools/u8g2.py extract --from my_font.c -o DIR    fonts in a bdfconv output file
    python3 tools/u8g2.py config --displays ssd1306_i2c_128x64_noname --fonts helvB10_tr,6x10_tf
                                                    the mcs_user_config.h / CMake / PlatformIO lines

NAME may be "helvB10_tr" or "u8g2_font_helvB10_tr" (u8x8: "u8x8_font_..." or --u8x8).
FILTER is a substring or a glob (ssd1306*i2c*). The sources are found in --u8g2 DIR,
$MICROCS_U8G2_DIR / $U8G2_DIR, third_party/u8g2, ../u8g2, build/third_party/u8g2-*,
build*/_microcs_deps/u8g2*, ~/Arduino/libraries/U8g2 - a clone of
github.com/olikraus/u8g2 (csrc/) or the Arduino library (src/clib/).
"""
import argparse, fnmatch, glob, io, os, re, sys, tarfile, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = "2.37.1"
URL = "https://github.com/olikraus/U8g2_Arduino/archive/refs/tags/{v}.tar.gz"


def csrc_of(d):
    """The folder with u8g2.h for a checkout / Arduino library / csrc folder, or None."""
    for sub in ("csrc", "src/clib", ""):
        c = os.path.join(d, sub)
        if os.path.isfile(os.path.join(c, "u8g2.h")) and os.path.isfile(os.path.join(c, "u8x8.h")):
            return os.path.abspath(c)
    return None


def candidates():
    for e in ("MICROCS_U8G2_DIR", "U8G2_DIR"):
        if os.environ.get(e):
            yield os.environ[e]
    for base in (os.getcwd(), ROOT):
        for sub in ("u8g2", "third_party/u8g2", "lib/u8g2", "external/u8g2", "components/u8g2", "../u8g2", "../U8g2"):
            yield os.path.join(base, sub)
        yield from sorted(glob.glob(os.path.join(base, "build/third_party/u8g2-*")), reverse=True)
        yield from sorted(glob.glob(os.path.join(base, "build*/_microcs_deps/u8g2*")), reverse=True)
    home = os.path.expanduser("~")
    for sub in ("Arduino/libraries/U8g2", "Documents/Arduino/libraries/U8g2", "Arduino/libraries/U8g2_Arduino"):
        yield os.path.join(home, sub)


def find(explicit=None):
    if explicit:
        c = csrc_of(explicit)
        if not c:
            sys.exit(f"u8g2: {explicit} has no u8g2.h (csrc/ or src/clib/) - point --u8g2 at the u8g2 sources")
        return c
    for d in candidates():
        c = csrc_of(d)
        if c:
            return c
    sys.exit("u8g2: the u8g2 sources were not found. Get them with\n"
             "    python3 tools/u8g2.py fetch            (or: make fetch-u8g2)\n"
             "or clone https://github.com/olikraus/u8g2 next to MicroCS, install the U8g2 Arduino\n"
             "library, or pass --u8g2 DIR / set MICROCS_U8G2_DIR.")


def fetch(version, out):
    dest = out or os.path.join(ROOT, "build", "third_party", f"u8g2-{version}")
    if csrc_of(dest):
        print(dest)
        return dest
    url = URL.format(v=version)
    print(f"u8g2: downloading {url}", file=sys.stderr)
    data = urllib.request.urlopen(url, timeout=120).read()
    os.makedirs(dest, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
        for m in t.getmembers():
            parts = m.name.split("/", 1)
            if len(parts) < 2 or not parts[1] or ".." in parts[1].split("/"):
                continue
            if not (parts[1].startswith("src/clib/") or parts[1] in ("LICENSE", "library.properties")):
                continue
            m.name = parts[1]
            if hasattr(tarfile, "data_filter"):
                t.extract(m, dest, filter="data")
            else:
                t.extract(m, dest)
    if not csrc_of(dest):
        sys.exit(f"u8g2: {url} did not contain src/clib/u8g2.h")
    print(dest)
    return dest


def matches(name, flt):
    if not flt:
        return True
    flt = flt.lower()
    n = name.lower()
    return fnmatch.fnmatch(n, flt) if any(c in flt for c in "*?[") else flt in n


def displays(csrc):
    """name -> (i2c, width, height, full buffer bytes)"""
    src = open(os.path.join(csrc, "u8g2_d_setup.c"), encoding="latin-1").read()
    out = {}
    for m in re.finditer(r"void u8g2_Setup_(\w+)_f\(.*?\)\s*\{(.*?)\n\}", src, re.S):
        name, body = m.group(1), m.group(2)
        b = re.search(r"u8g2_m_(\d+)_(\d+)_f", body)
        tw, th = (int(b.group(1)), int(b.group(2))) if b else (0, 0)
        out[name] = ("_i2c" in name, tw * 8, th * 8, tw * th * 8)
    return out


FONT_DEF = re.compile(rb"const uint8_t (u8(?:g2|x8)_font_\w+)\[(\d+)\][^=]*=\s*((?:\"(?:[^\"\\]|\\.)*\"\s*)+);", re.S)


def font_sources(csrc, x):
    return [os.path.join(csrc, "u8x8_fonts.c" if x else "u8g2_fonts.c")]


def font_index(csrc, x):
    """name -> size, from the declarations (fast)"""
    out = {}
    for f in font_sources(csrc, x):
        for m in re.finditer(rb"const uint8_t (u8(?:g2|x8)_font_\w+)\[(\d+)\]", open(f, "rb").read()):
            out[m.group(1).decode()] = int(m.group(2))
    return out


ESC = {b"n": 10, b"t": 9, b"r": 13, b"a": 7, b"b": 8, b"f": 12, b"v": 11, b"\\": 92, b"\"": 34, b"'": 39, b"?": 63}


def decode(lits):
    out = bytearray()
    for lit in re.findall(rb"\"((?:[^\"\\]|\\.)*)\"", lits, re.S):
        i, n = 0, len(lit)
        while i < n:
            c = lit[i]
            if c != 92:
                out.append(c); i += 1; continue
            e = lit[i + 1:i + 2]
            if e in ESC:
                out.append(ESC[e]); i += 2
            elif e == b"x":
                j = i + 2
                while j < n and j < i + 4 and chr(lit[j]) in "0123456789abcdefABCDEF":
                    j += 1
                out.append(int(lit[i + 2:j], 16) & 255); i = j
            else:
                j = i + 1
                while j < n and j < i + 4 and 48 <= lit[j] <= 55:
                    j += 1
                out.append(int(lit[i + 1:j], 8) & 255); i = j
    return bytes(out)


def fonts_in(text):
    """[(name, bytes)] of every font definition in C source text (bytes incl. the trailing NUL)"""
    res = []
    for m in FONT_DEF.finditer(text):
        data = decode(m.group(3)) + b"\0"
        size = int(m.group(2))
        if len(data) != size:
            sys.exit(f"u8g2: {m.group(1).decode()}: decoded {len(data)} bytes, declared {size}")
        res.append((m.group(1).decode(), data))
    return res


def full_name(n, x):
    if n.startswith(("u8g2_font_", "u8x8_font_")):
        return n
    return ("u8x8_font_" if x else "u8g2_font_") + n


def cmd_extract(a):
    if not a.out:
        sys.exit("u8g2 extract: -o DIR is required")
    os.makedirs(a.out, exist_ok=True)
    found = []
    if a.from_file:
        for f in a.from_file:
            found += fonts_in(open(f, "rb").read())
        want = set(a.names)
        if want:
            found = [(n, d) for n, d in found if n in want or n in {full_name(w, a.u8x8) for w in want}]
    else:
        csrc = find(a.u8g2)
        want = {full_name(n, a.u8x8 or n.startswith("u8x8_")) for n in a.names}
        kinds = [False, True] if a.all else sorted({n.startswith("u8x8_") for n in want})
        for x in kinds:
            for f in font_sources(csrc, x):
                text = open(f, "rb").read()
                if a.all:
                    found += fonts_in(text)
                else:
                    for n in want:
                        i = text.find(b"const uint8_t " + n.encode() + b"[")
                        m = FONT_DEF.match(text, i) if i >= 0 else None
                        if m:
                            found += fonts_in(m.group(0))
        missing = sorted(want - {n for n, _ in found}) if not a.all else []
        if missing:
            sys.exit("u8g2: unknown font(s): " + ", ".join(missing) + " - see `python3 tools/u8g2.py fonts FILTER`")
    total = 0
    for n, d in found:
        with open(os.path.join(a.out, n + ".bin"), "wb") as o:
            o.write(d)
        total += len(d)
        if not a.all:
            print(f"{a.out}/{n}.bin  {len(d)} bytes")
    print(f"u8g2: {len(found)} font file(s), {total} bytes in {a.out}", file=sys.stderr)


def cmd_config(a):
    csrc = find(a.u8g2) if a.u8g2 or not a.no_check else None
    ds = [d for d in (a.displays or "").replace(" ", ",").split(",") if d]
    fs = [full_name(f, False) for f in (a.fonts or "").replace(" ", ",").split(",") if f]
    xs = [full_name(f, True) for f in (a.u8x8_fonts or "").replace(" ", ",").split(",") if f]
    if csrc:
        known = displays(csrc)
        bad = [d for d in ds if d not in known]
        gi, xi = font_index(csrc, False), font_index(csrc, True)
        bad += [f for f in fs if f not in gi] + [f for f in xs if f not in xi]
        if bad:
            sys.exit("u8g2: unknown name(s): " + ", ".join(bad))
    dl = " ".join(f"U8G2_DISPLAY({d})" for d in ds)
    fl = " ".join(f"U8G2_FONT({f})" for f in fs)
    xl = " ".join(f"U8X8_FONT({f})" for f in xs)
    print("/* mcs_user_config.h */\n#define MCS_ENABLE_U8G2 1")
    if ds: print(f"#define MCS_U8G2_DISPLAYS {dl}")
    if fs: print(f"#define MCS_U8G2_FONTS {fl}")
    if xs: print(f"#define MCS_U8X8_FONTS {xl}")
    print("\n# CMake\n-DMICROCS_U8G2=ON" + (f' "-DMICROCS_U8G2_DISPLAYS={";".join(ds)}"' if ds else "")
          + (f' "-DMICROCS_U8G2_FONTS={";".join(fs)}"' if fs else "") + (f' "-DMICROCS_U8X8_FONTS={";".join(xs)}"' if xs else ""))
    print("\n# make\nU8G2=1" + (f' U8G2_DISPLAYS="{" ".join(ds)}"' if ds else "") + (f' U8G2_FONTS="{" ".join(fs)}"' if fs else "")
          + (f' U8X8_FONTS="{" ".join(xs)}"' if xs else ""))
    print("\n; platformio.ini\nlib_deps = olikraus/U8g2\nbuild_flags = -DMCS_ENABLE_U8G2=1"
          + (f" '-DMCS_U8G2_DISPLAYS={dl}'" if ds else "") + (f" '-DMCS_U8G2_FONTS={fl}'" if fs else "")
          + (f" '-DMCS_U8X8_FONTS={xl}'" if xs else ""))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--u8g2", metavar="DIR", help="u8g2 sources (checkout, csrc/ or Arduino library)")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("where")
    f = sub.add_parser("fetch"); f.add_argument("--version", default=VERSION); f.add_argument("-o", "--out")
    d = sub.add_parser("displays"); d.add_argument("filter", nargs="?")
    d.add_argument("--names", action="store_true", help="names only")
    fo = sub.add_parser("fonts"); fo.add_argument("filter", nargs="?"); fo.add_argument("--u8x8", action="store_true")
    fo.add_argument("--names", action="store_true", help="names only")
    e = sub.add_parser("extract"); e.add_argument("names", nargs="*"); e.add_argument("-o", "--out")
    e.add_argument("--all", action="store_true"); e.add_argument("--u8x8", action="store_true")
    e.add_argument("--from", dest="from_file", action="append", metavar="FILE.c")
    c = sub.add_parser("config"); c.add_argument("--displays"); c.add_argument("--fonts"); c.add_argument("--u8x8-fonts")
    c.add_argument("--no-check", action="store_true", help="do not check the names against the sources")
    a = p.parse_args()
    if a.cmd == "where":
        print(find(a.u8g2))
    elif a.cmd == "fetch":
        fetch(a.version, a.out)
    elif a.cmd == "displays":
        ds = displays(find(a.u8g2))
        rows = [(n, v) for n, v in sorted(ds.items()) if matches(n, a.filter)]
        for n, (i2c, w, h, ram) in rows:
            print(n if a.names else f"{n:44} {'I2C' if i2c else 'SPI/parallel':13} {w}x{h}  {ram} B buffer")
        if not a.names:
            print(f"{len(rows)} of {len(ds)} displays. Add the ones you use to MCS_U8G2_DISPLAYS: U8G2_DISPLAY(name)", file=sys.stderr)
    elif a.cmd == "fonts":
        idx = font_index(find(a.u8g2), a.u8x8)
        rows = [(n, s) for n, s in sorted(idx.items(), key=lambda t: t[0].lower()) if matches(n, a.filter)]
        for n, s in rows:
            print(n if a.names else f"{n:48} {s:6} bytes")
        if not a.names:
            print(f"{len(rows)} of {len(idx)} fonts. Built in: MCS_U8{'X8' if a.u8x8 else 'G2'}_FONTS; others: tools/u8g2.py extract NAME -o DIR", file=sys.stderr)
    elif a.cmd == "extract":
        if not a.names and not a.all and not a.from_file:
            sys.exit("u8g2 extract: font names, --all or --from FILE.c")
        cmd_extract(a)
    elif a.cmd == "config":
        cmd_config(a)


if __name__ == "__main__":
    main()
