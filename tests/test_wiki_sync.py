"""Wiki inventory, navigation and link rewriting regressions (no network)."""
import importlib.util
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wiki_sync', ROOT / 'tools/wiki_sync.py')
wiki = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wiki)
plist = wiki.pages()
sources = wiki.documentation_sources()
assert not wiki.inventory_errors(plist, sources)
assert {src for _, src, _ in plist} == sources
assert 'docs/CAN.md' in sources and 'docs/ENCODER.md' in sources
assert 'docs/MEMORY.md' in sources and 'tools/studio/templates/README.md' in sources
assert 'bench/compare/README.md' in sources and 'examples/lowram/README.md' in sources
assert any('missing from SECTIONS' in e for e in wiki.inventory_errors(plist, sources | {'docs/NEW.md'}))
assert any('duplicate source' in e for e in wiki.inventory_errors(plist + [plist[0]], sources))
assert any('duplicate/reserved' in e for e in wiki.inventory_errors(plist + [('Test','README.md','home')], sources))
assert any('missing:' in e for e in wiki.inventory_errors(plist + [('Test','docs/MISSING.md','Missing')], sources))
page_of = {src: name for _, src, name in plist}
for filename, label in [('docs/CAN.md','CAN-Bus'), ('docs/MEMORY.md','Memory-Budgets'),
                        ('tools/studio/templates/README.md','Studio-Template-Authoring')]:
    assert page_of[filename] == label
    assert '](' + label + ')' in wiki.home(plist)
    assert '](' + label + ')' in wiki.sidebar(plist)
errors = []
body = wiki.convert('docs/README.md', '[CAN](CAN.md)\n[code](../include/mcs.h)\n````\n[raw](missing.md)\n````', page_of, errors, {})
assert '[CAN](CAN-Bus)' in body
assert '/blob/main/include/mcs.h' in body
assert '[raw](missing.md)' in body and not errors
wiki.convert('docs/README.md', '[bad](MISSING.md)', page_of, errors, {})
assert any('broken link' in e for e in errors)
errors = []
wiki.convert('docs/README.md', '[bad](#no-such-anchor)', page_of, errors, {})
assert any('no heading' in e for e in errors)
# A source archive without .git still gets complete inventory checks.
oldroot = wiki.ROOT
with tempfile.TemporaryDirectory() as tmp:
    wiki.ROOT = tmp
    (Path(tmp)/'docs').mkdir()
    (Path(tmp)/'docs'/'GUIDE.md').write_text('# Guide\n')
    (Path(tmp)/'build').mkdir()
    (Path(tmp)/'build'/'ignored.md').write_text('# Build output\n')
    assert wiki.documentation_sources() == {'docs/GUIDE.md'}
wiki.ROOT = oldroot
assert 'Development preview' not in wiki.home(plist)
oldbranch = wiki.BRANCH
wiki.BRANCH = 'feat/example'
assert 'Development preview' in wiki.home(plist)
assert 'feat/example' in wiki.home(plist)
wiki.BRANCH = oldbranch
print(f'PASS wiki: {len(sources)} documents, complete inventory, navigation, links, anchors and archive fallback')
