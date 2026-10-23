#!/usr/bin/env python3
"""Build tools/studio/templates.js (the Studio template gallery) from tools/studio/templates/.

Layout: one folder per category, ordered by its number prefix (``07-uart``); every template is a
plain file in it (``echo.cs``, ``jobs.cfg``) and the folder's ``templates.txt`` describes them all:

    category: UART                      display name of the category

    [echo.cs] Echo bytes back           a file of this folder + the name shown in Studio
    save as: main.cs                    optional: the file name Studio suggests
    Read what arrives on UART 1 and     description: one or more lines
    send it back.

Lines starting with ``#`` are comments. Studio is a static page that also runs from file://, where
it cannot list or fetch folders, so the templates are bundled into templates.js - run this after
editing a template (CI fails when templates.js is out of date):

    python3 tools/studio/gen_templates.py            # write tools/studio/templates.js
    python3 tools/studio/gen_templates.py --check    # only verify it is up to date
    python3 tools/studio/gen_templates.py --list     # print category / file / name
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "templates")
OUT = os.path.join(HERE, "templates.js")
CAPABILITIES = set(re.findall(r'^MCS_CAP\("([^"\n]+)"', open(os.path.join(HERE, "../../include/mcs_capabilities.def"), encoding="utf-8").read(), re.M))

def requirements(value, where):
    names = [] if value.strip().lower() == "none" else [x.strip() for x in value.split(",")]
    if any(not x or x not in CAPABILITIES for x in names):
        raise TemplateError(f"{where}: requires: uses an unknown capability (names are case-sensitive)")
    if len(names) != len(set(names)):
        raise TemplateError(f"{where}: duplicate capability in requires:")
    return names

HEADER_RE = re.compile(r"^\[([^\]/\\]+)\]\s+(\S.*)$")


class TemplateError(Exception):
    pass


def parse_txt(folder, path):
    """templates.txt -> (category, [(file, name, save_as, desc)])"""
    cat, items, cur, defaults = None, [], None, None
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    for no, raw in enumerate(lines, 1):
        line = raw.rstrip()
        where = f"{os.path.relpath(path, HERE)}:{no}"
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = HEADER_RE.match(line)
        if m:
            cur = {"file": m.group(1).strip(), "name": m.group(2).strip(), "save": None, "desc": [], "where": where, "requires": None}
            items.append(cur)
        elif cur is None:
            k, _, v = line.partition(":")
            if k.strip().lower() == "requires":
                if defaults is not None: raise TemplateError(f"{where}: duplicate folder requires:")
                defaults = requirements(v, where)
            elif k.strip().lower() == "category" and v.strip():
                if cat is not None: raise TemplateError(f"{where}: duplicate category:")
                cat = v.strip()
            else:
                raise TemplateError(f"{where}: expected category: or requires: before the first [file] block")
        elif re.match(r"^min firmware\s*:", line, re.I):
            version = line.split(":", 1)[1].strip()
            if cur["desc"] or "minFirmware" in cur or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
                raise TemplateError(f"{where}: min firmware: needs one major.minor.patch before the description")
            cur["minFirmware"] = version
        elif re.match(r"^requires\s*:", line, re.I):
            if cur["desc"]: raise TemplateError(f"{where}: requires: must precede the description")
            if cur["requires"] is not None: raise TemplateError(f"{where}: duplicate requires:")
            cur["requires"] = requirements(line.split(":", 1)[1], where)
        elif re.match(r"^save as\s*:", line, re.I) and not cur["desc"]:
            cur["save"] = line.split(":", 1)[1].strip()
            if not cur["save"] or "/" in cur["save"]:
                raise TemplateError(f"{where}: 'save as:' needs a plain file name")
        else:
            cur["desc"].append(line.strip())
    if not cat:
        raise TemplateError(f"{os.path.relpath(path, HERE)}: no 'category: <name>' line")
    if defaults is None:
        raise TemplateError(f"{path}: add folder requires: <capabilities> (or none)")
    for it in items:
        if it["requires"] is None: it["requires"] = list(defaults)
        if it["file"].endswith(".cs") and "Compiler" not in it["requires"]:
            it["requires"].insert(0, "Compiler")
    return cat, items


def load():
    if not os.path.isdir(SRC):
        raise TemplateError(f"{SRC} is missing")
    templates, cats = [], set()
    folders = sorted(d for d in os.listdir(SRC) if os.path.isdir(os.path.join(SRC, d)))
    for folder in folders:
        if not re.match(r"^\d+-[a-z0-9-]+$", folder):
            raise TemplateError(f"templates/{folder}: folder names are '<number>-<slug>' (e.g. 07-uart)")
        fdir = os.path.join(SRC, folder)
        txt = os.path.join(fdir, "templates.txt")
        if not os.path.isfile(txt):
            raise TemplateError(f"templates/{folder}: templates.txt is missing")
        cat, items = parse_txt(folder, txt)
        if cat in cats:
            raise TemplateError(f"templates/{folder}: category '{cat}' is used by another folder too")
        cats.add(cat)
        listed = set()
        for it in items:
            if it["file"] in listed:
                raise TemplateError(f"{it['where']}: {it['file']} is listed twice")
            listed.add(it["file"])
            fp = os.path.join(fdir, it["file"])
            if not os.path.isfile(fp):
                raise TemplateError(f"{it['where']}: templates/{folder}/{it['file']} does not exist")
            if not it["desc"]:
                raise TemplateError(f"{it['where']}: {it['file']} has no description")
            with open(fp, encoding="utf-8", newline="") as f:
                text = f.read().replace("\r\n", "\n")
            templates.append({**({"minFirmware": it["minFirmware"]} if "minFirmware" in it else {}), "cat": cat, "name": it["name"], "file": it["save"] or it["file"],
                              "desc": " ".join(it["desc"]), "requires": it["requires"], "src": f"{folder}/{it['file']}", "text": text})
        extra = sorted(f for f in os.listdir(fdir) if f != "templates.txt" and not f.startswith(".") and f not in listed)
        if extra:
            raise TemplateError(f"templates/{folder}: not described in templates.txt: {', '.join(extra)}")
        if not items:
            raise TemplateError(f"templates/{folder}: no templates")
    return templates


def render(templates):
    out = ["/* GENERATED by tools/studio/gen_templates.py from tools/studio/templates/ - do not edit.",
           " * Add or change a template there (one folder per category, templates.txt describes the",
           " * files), then run: python3 tools/studio/gen_templates.py",
           " * Every C# template is compiled and run on the simulator by tests/studio/test_templates.js. */",
           f"const TEMPLATES = [  // {len(templates)} templates"]
    for t in templates:
        out.append(json.dumps(t, ensure_ascii=False) + ",")
    out.append("];")
    return "\n".join(out) + "\n"


def main(argv):
    try:
        templates = load()
    except TemplateError as e:
        print(f"gen_templates: {e}", file=sys.stderr)
        return 1
    if "--list" in argv:
        for t in templates:
            print(f"{t['cat']:<28} {t['src']:<52} {t['name']}")
        return 0
    text = render(templates)
    old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else ""
    if "--check" in argv:
        if old != text:
            print("gen_templates --check: tools/studio/templates.js is out of date - run python3 tools/studio/gen_templates.py", file=sys.stderr)
            return 1
        print(f"gen_templates --check: OK ({len(templates)} templates)")
        return 0
    if old != text:
        with open(OUT, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print(f"tools/studio/templates.js: {len(templates)} templates in {len({t['cat'] for t in templates})} categories")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
