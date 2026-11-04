"""Release publisher regression checks (no network)."""
import importlib.util
import json
import hashlib
import io
from pathlib import Path
import tempfile
import re
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sync', ROOT / 'tools/sync_flasher_releases.py')
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)

class CatalogTest(unittest.TestCase):
    def test_exact_names(self):
        boards = json.loads((ROOT / 'tools/flasher/boards.json').read_text())
        names = sync.expected_assets('v1.19.0', boards)
        self.assertEqual(len(names), 22)
        self.assertEqual(names['microcs-1.19.0-esp32c61.bin'], 'esp32c61')
        for tag in ['../../evil', 'v1.0/evil', 'latest']:
            with self.assertRaises(ValueError): sync.expected_assets(tag, boards)

    def test_registry_matches_release_workflow(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        esp = set(re.findall(r'target: (esp32[a-z0-9]*)', workflow))
        pico = set(re.findall(r'board: (pico2?_?w?|waveshare_rp2040_zero)[, }]', workflow))
        stm = set(re.search(r'board: \[(nucleo_[^\]]+)\]', workflow).group(1).split(', '))
        boards = json.loads((ROOT / 'tools/flasher/boards.json').read_text())
        self.assertEqual(esp | pico | stm, {b['id'] for b in boards})

    def test_empty_latest_fails_closed(self):
        latest = dict(tag_name='v1.19.0', id=1, assets=[], html_url='https://github.com/a/b/releases/tag/v1.19.0', published_at='2026-10-29')
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', side_effect=[latest, []]):
            with self.assertRaisesRegex(ValueError, 'no recognized firmware'): sync.sync(Path(d), 'a/b')
            self.assertFalse((Path(d) / 'firmware/catalog.json').exists())

    def release(self, tag='v1.19.0', ident=1, data=b'firmware'):
        name = f'microcs-{tag[1:]}-esp32.bin'
        return dict(id=ident, tag_name=tag, draft=False, prerelease=False,
                    html_url=f'https://github.com/a/b/releases/tag/{tag}',
                    published_at='2026-10-29T00:00:00Z', assets=[dict(
                        name=name, size=len(data), digest='sha256:' + hashlib.sha256(data).hexdigest(),
                        browser_download_url=f'https://github.com/a/b/releases/download/{tag}/{name}')])

    def test_mirror_exact_bytes_hash_and_no_token_on_binary_request(self):
        release = self.release()
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', return_value=release) as api, \
                patch.dict('os.environ', {'GH_TOKEN': 'private-token'}), \
                patch.object(sync.urllib.request, 'urlopen', return_value=io.BytesIO(b'firmware')) as download:
            sync.sync(Path(d), 'a/b', limit=1)
            catalog = json.loads((Path(d) / 'firmware/catalog.json').read_text())
            asset = catalog['releases'][0]['assets'][0]
            self.assertEqual((Path(d) / asset['path']).read_bytes(), b'firmware')
            self.assertEqual(asset['sha256'], hashlib.sha256(b'firmware').hexdigest())
            self.assertNotIn('Authorization', download.call_args.args[0].headers)
            api.assert_called_once_with('releases/latest', 'a/b')

    def test_history_skips_legacy_tags_empty_releases_and_prereleases(self):
        latest = self.release()
        legacy = self.release('old-release', 2)
        empty = self.release('v1.18.9', 3)
        empty['assets'] = []
        draft = self.release('v1.18.8', 4)
        draft['draft'] = True
        prerelease = self.release('v1.18.7', 5)
        prerelease['prerelease'] = True
        older = self.release('v1.18.6', 6)
        oldest = self.release('v1.18.5', 7)
        history = [latest, legacy, empty, draft, prerelease, older, oldest]
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', side_effect=[latest, history]), \
                patch.object(sync.urllib.request, 'urlopen', side_effect=lambda *a, **k: io.BytesIO(b'firmware')):
            sync.sync(Path(d), 'a/b')
            catalog = json.loads((Path(d) / 'firmware/catalog.json').read_text())
            self.assertEqual([r['tag'] for r in catalog['releases']], ['v1.19.0', 'v1.18.6', 'v1.18.5'])

    def test_history_pages_past_first_hundred_and_deduplicates(self):
        latest = self.release()
        prereleases = [dict(self.release(ident=i), prerelease=True) for i in range(2, 102)]
        older = self.release('v1.18.0', 102)
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', side_effect=[latest, prereleases, [latest, older, older]]) as api, \
                patch.object(sync.urllib.request, 'urlopen', side_effect=lambda *a, **k: io.BytesIO(b'firmware')):
            sync.sync(Path(d), 'a/b')
            catalog = json.loads((Path(d) / 'firmware/catalog.json').read_text())
            self.assertEqual(len(catalog['releases']), 2)
            self.assertEqual(api.call_args.args[0], 'releases?per_page=100&page=2')

    def test_fail_closed_for_asset_metadata_and_download_errors(self):
        changes = [dict(size=0), dict(size=True), dict(size=16777217),
                   dict(digest='sha256:' + '0' * 64), dict(size=7),
                   dict(browser_download_url='https://evil.example/firmware.bin'),
                   dict(browser_download_url='https://github.com/a/b/releases/download/v1.19.0/microcs-1.19.0-esp32.bin?redirect=1')]
        for change in changes:
            with self.subTest(change=change):
                latest = self.release()
                latest['assets'][0].update(change)
                with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', return_value=latest), \
                        patch.object(sync.urllib.request, 'urlopen', side_effect=lambda *a, **k: io.BytesIO(b'firmware')):
                    with self.assertRaises(ValueError): sync.sync(Path(d), 'a/b', limit=1)
                    self.assertFalse((Path(d) / 'firmware/catalog.json').exists())

    def test_duplicate_asset_names_fail_closed(self):
        latest = self.release()
        latest['assets'] *= 2
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', return_value=latest), \
                patch.object(sync.urllib.request, 'urlopen', return_value=io.BytesIO(b'firmware')):
            with self.assertRaisesRegex(ValueError, 'Duplicate firmware'): sync.sync(Path(d), 'a/b', limit=1)
            self.assertFalse((Path(d) / 'firmware/catalog.json').exists())

    def test_missing_github_digest_is_hashed_and_unknown_assets_not_fetched(self):
        latest = self.release()
        latest['assets'][0].pop('digest')
        latest['assets'].append(dict(name='microcs-1.19.0-unknown.bin',
                                     browser_download_url='https://evil.example/not-allowlisted'))
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', return_value=latest), \
                patch.object(sync.urllib.request, 'urlopen', return_value=io.BytesIO(b'firmware')) as download:
            sync.sync(Path(d), 'a/b', limit=1)
            catalog = json.loads((Path(d) / 'firmware/catalog.json').read_text())
            self.assertEqual(len(catalog['releases'][0]['assets']), 1)
            self.assertEqual(catalog['releases'][0]['assets'][0]['sha256'], hashlib.sha256(b'firmware').hexdigest())
            download.assert_called_once()

    def test_encoded_release_tag_is_valid(self):
        latest = self.release('v1.19.0+build.1')
        latest['assets'][0]['browser_download_url'] = latest['assets'][0]['browser_download_url'].replace('+', '%2B')
        with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', return_value=latest), \
                patch.object(sync.urllib.request, 'urlopen', return_value=io.BytesIO(b'firmware')):
            sync.sync(Path(d), 'a/b', limit=1)
            self.assertTrue((Path(d) / 'firmware/catalog.json').exists())

    def test_latest_prerelease_or_unsupported_tag_fails_closed(self):
        for latest in [dict(self.release(), prerelease=True), self.release('legacy')]:
            with tempfile.TemporaryDirectory() as d, patch.object(sync, 'api', return_value=latest):
                with self.assertRaises(ValueError): sync.sync(Path(d), 'a/b')
                self.assertFalse((Path(d) / 'firmware/catalog.json').exists())

    def test_invalid_repo(self):
        with self.assertRaises(ValueError): sync.sync(Path('/unused'), '../bad/repo')

if __name__ == '__main__': unittest.main()
