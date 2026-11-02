#!/usr/bin/env python3
"""Generate the LVGL binding tables (modules/lvgl/mcs_lvgl_api.inc) - the file is generated, never committed.

  python3 tools/lvgl_gen/build_api.py --lvgl <lvgl checkout> [--out FILE]
        only for that LVGL (what `make mcs-lvgl` / the firmware builds do): small, fast
  python3 tools/lvgl_gen/build_api.py --all [--work build/lvgl-api] [--out FILE]
        every supported release (v8.0.0 ... latest, cloned shallow from GitHub): one file for any 8.x / 9.x
  python3 tools/lvgl_gen/build_api.py --dirs v8.3.11=/path/a v9.2.2=/path/b ...   explicit checkouts

Needs python3, `pip install pycparser` and gcc (the preprocessor) - only here, never in the firmware build."""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

TAGS = ['v8.0.0', 'v8.1.0', 'v8.2.0', 'v8.3.0', 'v8.3.11', 'v8.4.0', 'v9.0.0', 'v9.1.0', 'v9.2.0', 'v9.2.2',
        'v9.3.0', 'v9.4.0', 'v9.5.0', 'v9.6.0']


def extract_one(tag, root, work, dbdir):
    import extract
    os.makedirs(dbdir, exist_ok=True)
    out = os.path.join(dbdir, tag + '.json')
    db = extract.extract(root, os.path.join(work, 'x-' + tag), tag)
    with open(out, 'w') as f:
        json.dump(db, f, indent=0)
    print('  %s: %d functions, %d enums, %d structs' % (tag, len(db['functions']), len(db['enums']), len(db['structs'])))


def clone(tag, dest):
    if os.path.isfile(os.path.join(dest, 'lvgl.h')):
        return
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    subprocess.check_call(['git', 'clone', '-q', '--depth', '1', '--branch', tag, 'https://github.com/lvgl/lvgl', dest])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--lvgl', help='one LVGL checkout')
    ap.add_argument('--all', action='store_true', help='all supported releases (git clone)')
    ap.add_argument('--dirs', nargs='*', help='TAG=DIR pairs')
    ap.add_argument('--work', default=os.path.join(REPO, 'build', 'lvgl-api'))
    ap.add_argument('--out', default=os.path.join(REPO, 'modules', 'lvgl', 'mcs_lvgl_api.inc'))
    ap.add_argument('--api-json', help='also write the API description (Studio IntelliSense, docs)')
    a = ap.parse_args()
    items = []
    if a.lvgl:
        import extract
        v = extract.read_version(a.lvgl)
        items.append(('v%d.%d.%d' % tuple(v), a.lvgl))
    if a.dirs:
        items += [d.split('=', 1) for d in a.dirs]
    if a.all:
        items += [(t, os.path.join(a.work, 'src', t)) for t in TAGS]
    if not items:
        ap.error('give --lvgl DIR, --all or --dirs')
    dbdir = os.path.join(a.work, 'db-' + ('all' if a.all else items[0][0]))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    for t, d in items:
        j = os.path.join(dbdir, t + '.json')
        if os.path.isfile(j) and os.path.getmtime(j) > os.path.getmtime(os.path.join(HERE, 'extract.py')):
            continue      # up to date (CI caches build/lvgl-api)
        if a.all:
            print('fetch', t); clone(t, d)
        print('extract', t); extract_one(t, d, a.work, dbdir)
    cmd = [sys.executable, os.path.join(HERE, 'gen.py'), dbdir, '--out', a.out]
    if a.api_json:
        cmd += ['--api-json', a.api_json]
    else:
        cmd += ['--api-json', os.path.join(a.work, 'lvgl_api.json')]
    subprocess.check_call(cmd)


if __name__ == '__main__':
    main()
