#!/usr/bin/env python3
"""Reject every sanitizer diagnostic, even when recovering UBSan exits zero.
The opt-in host dependency repair replaces the former LVGL 8 font exception.
"""
import argparse
import re
from pathlib import Path


def inspect(text, version):
    known, unexpected = [], []
    # Retain version argument/output shape for existing callers; no exceptions.
    del version
    for line in text.splitlines():
        if 'runtime error:' in line:
            unexpected.append(line)
        elif re.search(r'(?:ERROR|SUMMARY): (?:AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|ThreadSanitizer)|AddressSanitizer:DEADLYSIGNAL', line):
            unexpected.append(line)
    return known, unexpected

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lvgl-version', required=True)
    p.add_argument('log', type=Path)
    args = p.parse_args()
    known, unexpected = inspect(args.log.read_text(errors='replace'), args.lvgl_version)
    for line in unexpected:
        print('UNEXPECTED SANITIZER DIAGNOSTIC: ' + line.strip())
    if unexpected:
        raise SystemExit(1)
    print('Sanitizer diagnostic audit: no reports (no exceptions)')

if __name__ == '__main__':
    main()
