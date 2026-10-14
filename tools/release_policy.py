#!/usr/bin/env python3
"""Publication naming/ref policy. No network, credentials, tags or releases."""
import argparse
import re

STABLE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")
PREVIEW = re.compile(r"2\.0\.0-preview\.[1-9][0-9]*\Z")


def validate(channel, version, ref=None):
    if channel == "stable":
        if ref != "refs/heads/main":
            raise ValueError("Stable releases may only build/publish refs/heads/main.")
        if not STABLE.fullmatch(version):
            raise ValueError("Stable release version must be numeric MAJOR.MINOR.PATCH, not a preview.")
    elif channel == "preview":
        if not PREVIEW.fullmatch(version):
            raise ValueError("Preview version must be 2.0.0-preview.N (N starts at 1).")
    else:
        raise ValueError("Unknown release channel.")
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("channel", choices=["stable", "preview"])
    parser.add_argument("--version", required=True)
    parser.add_argument("--ref")
    args = parser.parse_args()
    try:
        print(validate(args.channel, args.version, args.ref))
    except ValueError as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
