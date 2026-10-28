#!/usr/bin/env python3
"""Fail unexpected sanitizer diagnostics, even when recovering UBSan exits zero.

No compiler/runtime diagnostics are disabled. The stock LVGL 8 compressed-font
bit writer has a documented signed-left-shift defect. Report that exact known
fixture diagnostic loudly, not as a clean UBSan pass; every other report fails.
"""
import argparse
import re
from pathlib import Path

KNOWN_FONT = re.compile(r'(?:^|[/\\])lv_font_fmt_txt\.c:\d+:\d+: runtime error: left shift of negative value -16\s*$')

def inspect(text, version):
    known, unexpected = [], []
    legacy = version.lstrip('v').split('.')[0] == '8'
    for line in text.splitlines():
        if 'runtime error:' in line:
            (known if legacy and KNOWN_FONT.search(line) else unexpected).append(line)
        elif re.search(r'(?:ERROR|SUMMARY): (?:AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|ThreadSanitizer)|AddressSanitizer:DEADLYSIGNAL', line):
            unexpected.append(line)
    return known, unexpected

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lvgl-version', required=True)
    p.add_argument('log', type=Path)
    args = p.parse_args()
    known, unexpected = inspect(args.log.read_text(errors='replace'), args.lvgl_version)
    for line in known:
        print('::warning::Known upstream LVGL 8 compressed-font signed-shift UB (NOT a clean UBSan pass): ' + line.strip())
    for line in unexpected:
        print('UNEXPECTED SANITIZER DIAGNOSTIC: ' + line.strip())
    if unexpected:
        raise SystemExit(1)
    print('Sanitizer diagnostic audit: no unexpected reports; known upstream reports=' + str(len(known)))

if __name__ == '__main__':
    main()
