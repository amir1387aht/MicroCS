#!/usr/bin/env python3
"""Test files for tests/lvgl/t07_fs*.cs: images (BMP, PNG, LVGL binary) and fonts (TrueType, LVGL binary
font made by lv_font_conv) written into <out>; `mcs --fs <out>` serves them to LVGL through the MicroCS VFS.

    python3 tests/lvgl/make_assets.py build/lvglfs [--lvgl <LVGL checkout>]

Fonts come from the LVGL checkout (scripts/built_in_font/Montserrat-Medium.ttf); the binary font needs
`npm install -g lv_font_conv` (or npx). Nothing here is committed: it is all generated."""
import argparse, os, shutil, struct, subprocess, sys, zlib


def bmp24(w, h, px):
    """px(x, y) -> (r, g, b); bottom-up 24 bit BMP"""
    row = (w * 3 + 3) & ~3
    data = bytearray()
    for y in range(h - 1, -1, -1):
        line = bytearray()
        for x in range(w):
            r, g, b = px(x, y)
            line += bytes((b, g, r))
        data += line + b'\0' * (row - len(line))
    hdr = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
    dib = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    return hdr + dib + bytes(data)


def png(w, h, px):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            raw += bytes(px(x, y)) + b'\xff'
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))


def lvgl8_bin(w, h, px):
    """LVGL 8 binary image, LV_IMG_CF_TRUE_COLOR (BGRA8888 with LV_COLOR_DEPTH 32)"""
    hdr = struct.pack('<I', 4 | (w << 10 & 0xFFFFFC00) >> 0)  # placeholder, rebuilt below
    cf = 4                                  # LV_IMG_CF_TRUE_COLOR
    word = (cf & 0x1f) | ((w & 0x7ff) << 10) | ((h & 0x7ff) << 21)
    data = bytearray(struct.pack('<I', word))
    for y in range(h):
        for x in range(w):
            r, g, b = px(x, y)
            data += bytes((b, g, r, 255))
    return bytes(data)


def lvgl9_bin(w, h, px):
    """LVGL 9 binary image, LV_COLOR_FORMAT_ARGB8888 (0x10)"""
    data = bytearray(struct.pack('<BBHHHHH', 0x19, 0x10, 0, w, h, w * 4, 0))
    for y in range(h):
        for x in range(w):
            r, g, b = px(x, y)
            data += bytes((b, g, r, 255))
    return bytes(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--lvgl')
    ap.add_argument('--no-font-conv', action='store_true')
    a = ap.parse_args()
    out = a.out
    for d in ('img', 'fonts', 'sub/dir'):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    def two(x, y):               # left half red, right half blue
        return (255, 0, 0) if x < 8 else (0, 0, 255)

    def grad(x, y):              # top green, bottom yellow
        return (0, 255, 0) if y < 8 else (255, 255, 0)

    w = lambda p, d: open(os.path.join(out, p), 'wb').write(d)
    w('img/two.bmp', bmp24(16, 16, two))
    w('img/grad.png', png(16, 16, grad))
    w('img/two8.bin', lvgl8_bin(16, 16, two))
    w('img/two9.bin', lvgl9_bin(16, 16, two))
    w('sub/dir/hello.txt', b'hello from the MicroCS file system\n')
    w('sub/a.txt', b'a')
    w('sub/b.txt', b'bb')

    ttf = None
    if a.lvgl:
        for n in ('Montserrat-Medium.ttf', 'DejaVuSans.ttf'):
            p = os.path.join(a.lvgl, 'scripts', 'built_in_font', n)
            if os.path.exists(p):
                ttf = p
                break
        if not ttf:   # newer LVGL: no scripts/built_in_font; its tiny_ttf / freetype examples ship TrueType files
            for n in ('examples/libs/tiny_ttf/Ubuntu-Medium.ttf', 'examples/libs/freetype/Lato-Regular.ttf'):
                p = os.path.join(a.lvgl, n)
                if os.path.exists(p):
                    ttf = p
                    break
    if ttf:
        shutil.copy(ttf, os.path.join(out, 'fonts', 'face.ttf'))
        if not a.no_font_conv:
            cmd = ['npx', '--yes', 'lv_font_conv', '--font', ttf, '-r', '0x20-0x7F', '--size', '16', '--bpp', '4',
                   '--format', 'bin', '--no-compress', '-o', os.path.join(out, 'fonts', 'face16.bin')]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit('lv_font_conv failed (npm install -g lv_font_conv, or --no-font-conv): ' + r.stderr[-500:])
    else:
        print('make_assets: no TrueType font (pass --lvgl <checkout>): font tests will be skipped', file=sys.stderr)
    print('assets in', out)


if __name__ == '__main__':
    main()
