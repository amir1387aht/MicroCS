#!/usr/bin/env python3
"""Build the GitHub wiki from the Markdown files in the repository.

    python3 tools/wiki_sync.py --out build/wiki     # write the wiki pages
    python3 tools/wiki_sync.py --check              # only check links (CI)

The documentation lives in the repository (docs/, docs/guides/, ports/*/README.md,
...) and is the only place to edit it; .github/workflows/wiki.yml runs this script on
every push to main and pushes the result to https://github.com/<repo>/wiki.

Every published .md becomes one wiki page; links between them become wiki links,
links to other files point at the file on GitHub, images at the raw file. --check
fails on links to files that do not exist and on #anchors that no heading defines.
"""
import argparse
import os
import re
import sys
import unicodedata
from urllib.parse import unquote

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.environ.get("GITHUB_REPOSITORY", "amir1387aht/MicroCS")
BRANCH = os.environ.get("MICROCS_WIKI_BRANCH", "main")

# source file -> wiki page name (order = sidebar order inside each section)
SECTIONS = [
    ("Start here", [
        ("README.md", "MicroCS"),
        ("docs/README.md", "Documentation"),
        ("docs/GETTING_STARTED.md", "Getting-Started"),
    ]),
    ("Step-by-step guides", [
        ("docs/guides/README.md", "Guides"),
        ("docs/guides/PC_SIMULATOR.md", "Guide-PC-Simulator"),
        ("docs/guides/ESP32.md", "Guide-ESP32"),
        ("docs/guides/ESP32_C3_OLED.md", "Guide-ESP32-C3-OLED"),
        ("docs/guides/PICO.md", "Guide-Raspberry-Pi-Pico"),
        ("docs/guides/STM32.md", "Guide-STM32"),
        ("docs/guides/ARDUINO.md", "Guide-Arduino"),
        ("docs/guides/ZEPHYR.md", "Guide-Zephyr"),
        ("docs/guides/PERIPHERALS.md", "Guide-Peripheral-Settings"),
        ("docs/guides/DMA.md", "Guide-DMA"),
    ]),
    ("Hardware", [
        ("docs/HAL.md", "Hardware-API"),
        ("docs/PERIPHERALS.md", "Peripheral-Configuration"),
        ("docs/DMA.md", "DMA"),
        ("docs/DRIVERS.md", "Device-Drivers"),
        ("docs/U8G2.md", "U8g2-Displays"),
        ("docs/FILESYSTEM.md", "Filesystem"),
        ("docs/THREADS.md", "Threads"),
        ("docs/SCHEDULER.md", "Scheduler"),
    ]),
    ("Ports", [
        ("docs/PORTING.md", "Porting-and-Build-Systems"),
        ("ports/esp32/README.md", "Port-ESP32"),
        ("ports/rp2/README.md", "Port-RP2040-RP2350"),
        ("ports/stm32/README.md", "Port-STM32"),
        ("ports/stm32/firmware/README.md", "Port-STM32-Firmware"),
        ("ports/zephyr/README.md", "Port-Zephyr"),
        ("ports/arduino/README.md", "Port-Arduino"),
        ("ports/template/README.md", "Port-Template"),
    ]),
    ("C# and the runtime", [
        ("docs/LANGUAGE.md", "Language"),
        ("docs/STDLIB.md", "Standard-Library"),
        ("docs/STANDALONE.md", "Standalone-Firmware"),
        ("tools/studio/README.md", "MicroCS-Studio"),
        ("examples/README.md", "Examples"),
    ]),
    ("Embedding and tuning", [
        ("docs/EMBEDDING.md", "Embedding"),
        ("docs/CONFIGURATION.md", "Configuration"),
        ("docs/LOW_RESOURCE.md", "Small-MCUs"),
        ("docs/PERFORMANCE.md", "Performance"),
    ]),
    ("Internals", [
        ("docs/ARCHITECTURE.md", "Architecture"),
        ("docs/BYTECODE.md", "Bytecode"),
        ("docs/SECURITY.md", "Security"),
        ("docs/TESTING.md", "Testing"),
    ]),
    ("Project", [
        ("CHANGELOG.md", "Changelog"),
        ("CONTRIBUTING.md", "Contributing"),
    ]),
]

LINK = re.compile(r"(!?)\[((?:[^\[\]]|\[[^\]]*\])*)\]\(([^)\s]+)(\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^\s*(```|~~~)")


def pages():
    out = []
    for title, items in SECTIONS:
        for src, name in items:
            if os.path.exists(os.path.join(ROOT, src)):
                out.append((title, src, name))
    return out


def slug(heading):
    """GitHub's anchor for a heading."""
    h = re.sub(r"<[^>]+>", "", heading.strip().lower())
    h = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", h)
    keep = []
    for ch in h:
        cat = unicodedata.category(ch)
        if ch in " -_" or cat[0] in "LM" or cat in ("Nd", "Nl"):
            keep.append(ch)
    return "".join(keep).replace(" ", "-")


def anchors(path):
    seen, out, fence = {}, set(), False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if FENCE.match(line):
                fence = not fence
                continue
            if fence:
                continue
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if m:
                s = slug(m.group(2))
                n = seen.get(s, 0)
                seen[s] = n + 1
                out.add(s if n == 0 else "%s-%d" % (s, n))
            for a in re.findall(r"<a\s+(?:name|id)=\"([^\"]+)\"", line):
                out.add(a)
    return out


def convert(src, text, page_of, errors, anchor_cache):
    base = os.path.dirname(src)
    lines, fence = [], False
    for line in text.split("\n"):
        if FENCE.match(line):
            fence = not fence
            lines.append(line)
            continue
        if fence:
            lines.append(line)
            continue

        def fix(m):
            bang, label, target, title = m.group(1), m.group(2), m.group(3), m.group(4) or ""
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.I) or target.startswith("//"):
                return m.group(0)
            path, _, frag = target.partition("#")
            frag = unquote(frag)
            if not path:                                   # same page
                if frag and frag not in anchor_cache.setdefault(src, anchors(os.path.join(ROOT, src))):
                    errors.append("%s: no heading for #%s" % (src, frag))
                return m.group(0)
            rel = os.path.normpath(os.path.join(base, path)).replace(os.sep, "/")
            full = os.path.join(ROOT, rel)
            if rel.startswith("../") or not os.path.exists(full):
                errors.append("%s: broken link %s" % (src, target))
                return m.group(0)
            if frag and rel.endswith(".md"):
                if frag not in anchor_cache.setdefault(rel, anchors(full)):
                    errors.append("%s: %s has no heading for #%s" % (src, rel, frag))
            if bang:
                url = "https://raw.githubusercontent.com/%s/%s/%s" % (REPO, BRANCH, rel)
            elif rel in page_of:
                url = page_of[rel] + ("#" + frag if frag else "")
            else:
                kind = "tree" if os.path.isdir(full) else "blob"
                url = "https://github.com/%s/%s/%s/%s" % (REPO, kind, BRANCH, rel.rstrip("/"))
                if frag:
                    url += "#" + frag
            return "%s[%s](%s%s)" % (bang, label, url, title)

        lines.append(LINK.sub(fix, line))
    return "\n".join(lines)


def home(plist):
    out = ["# MicroCS wiki", "",
           "C# for microcontrollers: a compact C# compiler and VM that runs on ESP32, Raspberry Pi Pico, STM32, "
           "nRF / Zephyr boards, Arduino and the PC.", "",
           "**New here?** Pick the guide for your board below - each one goes from an empty folder to running C# "
           "on the chip, step by step. The reference pages explain every option.", ""]
    for title, items in SECTIONS:
        rows = [(src, name) for (src, name) in items if any(p[1] == src for p in plist)]
        if not rows:
            continue
        out.append("## " + title)
        out.append("")
        for src, name in rows:
            out.append("* [%s](%s)" % (name.replace("-", " "), name))
        out.append("")
    out.append("These pages are generated from the Markdown files in the [repository](https://github.com/%s) "
               "(`docs/`, `docs/guides/`, `ports/*/README.md`); edit them there - changes to the wiki itself "
               "are overwritten on the next push to `%s`." % (REPO, BRANCH))
    return "\n".join(out) + "\n"


def sidebar(plist):
    out = ["**[Home](Home)**", ""]
    for title, items in SECTIONS:
        rows = [(src, name) for (src, name) in items if any(p[1] == src for p in plist)]
        if not rows:
            continue
        out.append("**%s**" % title)
        for src, name in rows:
            out.append("* [%s](%s)" % (name.replace("-", " "), name))
        out.append("")
    return "\n".join(out) + "\n"


def version():
    with open(os.path.join(ROOT, "include", "mcs.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+MCS_VERSION_STRING\s+"([^"]+)"', f.read())
    return m.group(1) if m else None


STALE = [re.compile(r"MicroCS[- ](\d+\.\d+\.\d+)\.zip"), re.compile(r"MicroCS (\d+\.\d+\.\d+) C# REPL"),
         re.compile(r"microcs-(\d+\.\d+\.\d+)-[a-z0-9_]+\.(?:bin|uf2|hex)"),
         re.compile(r"badge/version-(\d+\.\d+\.\d+)-")]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", help="folder for the wiki pages")
    ap.add_argument("--check", action="store_true", help="only check the links")
    a = ap.parse_args()
    plist = pages()
    page_of = {src: name for (_, src, name) in plist}
    errors, cache, result = [], {}, {}
    ver = version()
    for _, src, name in plist:
        with open(os.path.join(ROOT, src), encoding="utf-8") as f:
            text = f.read()
        body = convert(src, text, page_of, errors, cache)
        if src != "CHANGELOG.md" and ver:
            for rx in STALE:
                for m in rx.finditer(text):
                    if m.group(1) != ver:
                        errors.append("%s: says %s, the version is %s" % (src, m.group(0), ver))
        result[name + ".md"] = body.rstrip("\n") + "\n"
    result["Home.md"] = home(plist)
    result["_Sidebar.md"] = sidebar(plist)
    result["_Footer.md"] = ("Generated from the [MicroCS repository](https://github.com/%s) - "
                            "edit the files there, not in the wiki.\n" % REPO)
    for e in errors:
        print("wiki:", e, file=sys.stderr)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        keep = set(result)
        for old in os.listdir(a.out):
            if old.endswith(".md") and old not in keep:
                os.remove(os.path.join(a.out, old))
        for name, body in result.items():
            with open(os.path.join(a.out, name), "w", encoding="utf-8") as f:
                f.write(body)
        print("wiki: %d pages -> %s" % (len(result), a.out))
    elif not a.check:
        ap.error("give --out DIR or --check")
    if errors:
        print("wiki: %d problem(s)" % len(errors), file=sys.stderr)
        return 1
    if a.check:
        print("wiki: %d pages, links OK" % len(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
