"""Names the built package for a release: ps4/dist/Chronicle-PS4-<version>.pkg, and prints its SHA-256.

    python ps4/release.py v0.1.0

Needs ps4/out/<content id>.pkg (python ps4/package.py first). The release is that one file; it holds no game data.
The Piglet modules are inside it when it was built with --bundle-sprx.
"""
import hashlib
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CONTENT_ID = 'IV0000-DCLD00001_00-DARKCLOUDCHRON00'
version = sys.argv[1] if len(sys.argv) > 1 else 'dev'
package = HERE / 'out' / f'{CONTENT_ID}.pkg'
if not package.is_file():
    raise SystemExit(f'Missing {package}: run python ps4/package.py first.')

target = HERE / 'dist' / f'Chronicle-PS4-{version}.pkg'
target.parent.mkdir(exist_ok=True)
shutil.copyfile(package, target)
print(target)
print(hashlib.sha256(target.read_bytes()).hexdigest(), target.name)
