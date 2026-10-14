"""Assembles the release folder: ps4/dist/Chronicle-PS4-<version>/ and a zip of it.

    python ps4/release.py v0.1.0

Needs ps4/out/<content id>.pkg (python ps4/package.py first) and the Windows dcdata tool
(build/windows-x64-release/dcdata.exe), which lets people extract their ISO on a PC instead.
Holds no game data. The package itself carries the Piglet modules when it was built with --bundle-sprx.
"""
import hashlib
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CONTENT_ID = 'IV0000-DCLD00001_00-DARKCLOUDCHRON00'
version = sys.argv[1] if len(sys.argv) > 1 else 'dev'
package = HERE / 'out' / f'{CONTENT_ID}.pkg'
dcdata = ROOT / 'port/build/windows-x64-release/dcdata.exe'
for path in (package, dcdata):
    if not path.is_file():
        raise SystemExit(f'Missing {path}')

folder = HERE / 'dist' / f'Chronicle-PS4-{version}'
shutil.rmtree(folder, ignore_errors=True)
folder.mkdir(parents=True)
shutil.copyfile(package, folder / 'DarkCloud-PS4-01.00.pkg')
shutil.copyfile(dcdata, folder / 'dcdata.exe')
shutil.copyfile(HERE / 'docs/INSTALL.md', folder / 'INSTALL.md')
shutil.copyfile(HERE / 'docs/PACKAGE-CONTENTS.md', folder / 'PACKAGE-CONTENTS.md')
shutil.copyfile(HERE / 'LICENSE-ps4-port', folder / 'LICENSE-ps4-port')
sums = ''.join(f'{hashlib.file_digest(p.open("rb"), "sha256").hexdigest()}  {p.name}\n'
               for p in sorted(folder.iterdir()) if p.is_file())
(folder / 'SHA256SUMS.txt').write_text(sums)
archive = shutil.make_archive(str(folder), 'zip', folder.parent, folder.name)
print(archive)
print(sums)
