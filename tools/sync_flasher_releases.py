#!/usr/bin/env python3
"""Mirror official release assets to Pages: no browser CORS proxy or GitHub token.

Run after copying tools/flasher/{*.html,*.css,*.js,boards.json} to site/flash/.
Only exact, known firmware names are downloaded. Missing boards stay unavailable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit
import urllib.request


def expected_assets(tag, boards):
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?", tag):
        raise ValueError(f"Unsupported release tag: {tag!r}")
    return {f"microcs-{tag[1:]}-{b['id']}.{b['extension']}": b['id'] for b in boards}


def api(path, repo):
    request = urllib.request.Request(f"https://api.github.com/repos/{repo}/{path}", headers={
        "Accept": "application/vnd.github+json", "User-Agent": "MicroCS-Pages",
        **({"Authorization": f"Bearer {os.environ['GH_TOKEN']}"} if os.environ.get('GH_TOKEN') else {}),
    })
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def stable_releases(repo, latest):
    """Yield latest first, then distinct stable releases, paging past prereleases."""
    if latest.get('draft') or latest.get('prerelease'):
        raise ValueError('Latest release must be published and stable')
    yield latest
    seen = {latest['id']}
    page = 1
    while True:
        path = 'releases?per_page=100' + (f'&page={page}' if page > 1 else '')
        releases = api(path, repo)
        for release in releases:
            if release['id'] in seen:
                continue
            seen.add(release['id'])
            if not release['draft'] and not release['prerelease']:
                yield release
        if len(releases) < 100:
            return
        page += 1


def sync(destination, repo, limit=3):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Expected owner/repository")
    if limit not in range(1, 4):
        raise ValueError('Release limit must be between 1 and 3')
    boards = json.loads((Path(__file__).parent / 'flasher/boards.json').read_text())
    # GitHub's latest endpoint determines the default, not tag sorting or prereleases.
    latest = api('releases/latest', repo)
    catalog = {'schema': 1, 'repository': repo, 'releases': []}
    for release in stable_releases(repo, latest):
        tag = release['tag_name']
        try:
            names = expected_assets(tag, boards)
        except ValueError:
            if release['id'] == latest['id']:
                raise
            # Historical releases may predate the versioned firmware naming scheme.
            continue
        assets = []
        seen_assets = set()
        for asset in release['assets']:
            name = asset['name']
            if name not in names:
                continue
            if name in seen_assets:
                raise ValueError(f'Duplicate firmware asset: {name}')
            seen_assets.add(name)
            size = asset['size']
            if type(size) is not int or not 0 < size <= 16 * 1024 * 1024:
                raise ValueError(f'Unexpected firmware size: {name}')
            url = asset['browser_download_url']
            parsed = urlsplit(url)
            if (parsed.scheme != 'https' or parsed.netloc != 'github.com'
                    or parsed.query or parsed.fragment
                    or unquote(parsed.path) != f'/{repo}/releases/download/{tag}/{name}'):
                raise ValueError(f'Unexpected release URL: {url}')
            path = Path('firmware') / tag / name
            target = destination / path
            target.parent.mkdir(parents=True, exist_ok=True)
            # Public binary download only; never forward the API token to redirects.
            request = urllib.request.Request(url, headers={'User-Agent': 'MicroCS-Pages'})
            with urllib.request.urlopen(request, timeout=180) as response:
                data = response.read(16 * 1024 * 1024 + 1)
            digest = hashlib.sha256(data).hexdigest()
            if len(data) != size:
                raise ValueError(f'Truncated/oversized asset: {name}')
            official = asset.get('digest')
            if official and official != f'sha256:{digest}':
                raise ValueError(f'GitHub digest mismatch: {name}')
            target.write_bytes(data)
            assets.append(dict(board=names[name], name=name, path=path.as_posix(), size=size, sha256=digest, downloadUrl=url))
        if not assets and release['id'] == latest['id']:
            raise ValueError('Latest release has no recognized firmware; refusing stale deployment')
        if assets:
            catalog['releases'].append(dict(tag=tag, url=release['html_url'], publishedAt=release['published_at'], assets=assets))
        if len(catalog['releases']) == limit:
            break
    if not catalog['releases'] or catalog['releases'][0]['tag'] != latest['tag_name']:
        raise ValueError('Latest release has no recognized firmware; refusing stale deployment')
    out = destination / 'firmware/catalog.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(catalog, indent=2) + '\n')
    print(f"Mirrored {sum(len(r['assets']) for r in catalog['releases'])} images across {len(catalog['releases'])} releases")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dest', type=Path, default=Path('site/flash'))
    parser.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY', 'amir1387aht/MicroCS'))
    parser.add_argument('--limit', type=int, choices=range(1, 4), default=3)
    args = parser.parse_args()
    sync(args.dest, args.repo, args.limit)
