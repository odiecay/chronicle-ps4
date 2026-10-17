"""Builds the installable PS4 package of the Dark Cloud (Chronicle) port: ps4/out/<content id>.pkg.

Adapted from the Mario Kart PS4 packaging script in the same toolchain layout. The package holds the
game executable, the port's translated strings and button glyphs, and the Piglet pair when asked to; it
never holds the game's data or any disc content. Copy the extracted data (`dcdata extract`) to
/data/darkcloud/data on the console.

    python ps4/package.py                      # needs build/ps4/darkcloud.elf
    python ps4/package.py --bundle-sprx DIR    # also carries libScePigletv2VSH.sprx + libSceShaccVSH.sprx
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
# Where the tools are: OO_PS4_TOOLCHAIN is the OpenOrbis PS4 toolchain folder (the one holding bin/windows) and DOTNET
# is a dotnet runtime (leave it unset to use the one on PATH). See ps4/docs/BUILDING.md.
if not os.environ.get('OO_PS4_TOOLCHAIN'):
    raise SystemExit('Set OO_PS4_TOOLCHAIN to the OpenOrbis PS4 toolchain folder (see ps4/docs/BUILDING.md).')
OO = Path(os.environ['OO_PS4_TOOLCHAIN'])
BIN = OO / 'bin/windows'
_dotnet = os.environ.get('DOTNET') or shutil.which('dotnet')
if not _dotnet:
    raise SystemExit('dotnet was not found: put it on PATH or set DOTNET to dotnet.exe (see ps4/docs/BUILDING.md).')
DOTNET = Path(_dotnet)
ELF = ROOT / 'port/build/ps4/darkcloud.elf'
TITLE = 'Chronicle PS4 (Dark Cloud)'
TITLE_ID = 'DCLD00001'
CONTENT_ID = 'IV0000-DCLD00001_00-DARKCLOUDCHRON00'
# System auth info and program id used by the OpenOrbis Piglet sample: needed to load Piglet.
AUTHINFO = '000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'
PAID = '0x3800000000000035'

if not re.fullmatch(r'[A-Z]{4}[0-9]{5}', TITLE_ID):
    raise SystemExit('PS4 title ID must be four uppercase letters and five digits.')
if not re.fullmatch(r'[A-Z]{2}[0-9]{4}-' + TITLE_ID + r'_[0-9]{2}-[A-Z0-9]{16}', CONTENT_ID):
    raise SystemExit('PS4 content ID is invalid or disagrees with the title ID.')

parser = argparse.ArgumentParser()
parser.add_argument('--bundle-sprx', type=Path, help='directory holding libScePigletv2VSH.sprx and libSceShaccVSH.sprx')
parser.add_argument('--elf', type=Path, default=ELF, help='ELF from an isolated PS4 build')
parser.add_argument('--output', type=Path, default=HERE / 'out', help='fresh package output directory')
parser.add_argument('--app-version', default='01.07', help='PS4 application version, NN.NN')
args = parser.parse_args()
ELF = args.elf.resolve()
if not re.fullmatch(r'[0-9]{2}\.[0-9]{2}', args.app_version):
    raise SystemExit('--app-version must be NN.NN.')
output = args.output.resolve()
if (output / (CONTENT_ID + '.pkg')).exists() or (output / 'build-evidence.json').exists():
    raise SystemExit('Output already contains package evidence; choose a fresh --output directory.')

for path in (ELF, DOTNET):
    if not path.is_file():
        raise SystemExit(f'Missing {path}: build the PS4 target first (see ps4/docs/BUILDING.md).')
header = ELF.read_bytes()[:64]
if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != 62:
    raise SystemExit('Input must be a little-endian x86-64 ELF.')

env = os.environ.copy()
env['OO_PS4_TOOLCHAIN'] = str(OO)
env['DOTNET_ROOT'] = str(DOTNET.parent)
env['DOTNET_ROLL_FORWARD'] = 'LatestMajor'
env['DOTNET_CLI_TELEMETRY_OPTOUT'] = '1'
stage_root = Path(tempfile.gettempdir()) / 'chronicle-ps4-package-staging'
stage_root.mkdir(parents=True, exist_ok=True)
stage = Path(tempfile.mkdtemp(prefix='dcld-', dir=stage_root))
output.mkdir(parents=True, exist_ok=True)
for directory in ('sce_sys/about', 'sce_module'):
    (stage / directory).mkdir(parents=True)
shutil.copyfile(HERE / 'assets/icon0.png', stage / 'sce_sys/icon0.png')
shutil.copyfile(OO / 'samples/piglet/sce_sys/about/right.sprx', stage / 'sce_sys/about/right.sprx')
for name in ('libc.prx', 'libSceFios2.prx'):
    shutil.copyfile(OO / 'samples/piglet/sce_module' / name, stage / 'sce_module' / name)

files = ['eboot.bin', 'sce_sys/about/right.sprx', 'sce_sys/param.sfo', 'sce_sys/icon0.png',
         'sce_module/libc.prx', 'sce_module/libSceFios2.prx']

# The port's strings and button glyphs, which it reads beside the executable (/app0).
# Top-level files only: the package tool's file list cannot take nested directories.
for source, name in ((ROOT / 'port/lang', 'lang'), (ROOT / 'port/glyphs', 'glyphs')):
    (stage / name).mkdir()
    for path in sorted(source.iterdir()):
        if path.is_file():
            shutil.copyfile(path, stage / name / path.name)
            files.append(f'{name}/{path.name}')
# The setup screen's music (see port/src/platform/firstrun_ps4.cpp), 48 kHz Ogg Vorbis, read from /app0/music.
(stage / 'music').mkdir()
for path in sorted((HERE / 'assets/music').glob('*.ogg')):
    shutil.copyfile(path, stage / 'music' / path.name)
    files.append(f'music/{path.name}')
font = ROOT / 'tools/font/DarkCloudCompendium.ttf'
shutil.copyfile(font, stage / 'lang/font.ttf')
files.append('lang/font.ttf')
if args.bundle_sprx:
    (stage / 'assets/misc').mkdir(parents=True)
    for name in ('libScePigletv2VSH.sprx', 'libSceShaccVSH.sprx'):
        shutil.copyfile(args.bundle_sprx / name, stage / 'assets/misc' / name)
        files.append('assets/misc/' + name)
files = list(dict.fromkeys(files))


def run(arguments):
    result = subprocess.run([str(arg) for arg in arguments], cwd=stage, env=env,
                            creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout, flush=True)
    if result.stderr:
        print(result.stderr, flush=True)
    result.check_returncode()


run([BIN / 'create-fself.exe', '-in=' + str(ELF), '-out=' + str(stage / 'darkcloud.oelf'),
     '--eboot', 'eboot.bin', '--paid', PAID, '--authinfo', AUTHINFO])
sfo = 'sce_sys/param.sfo'
pkgtool = [DOTNET, BIN / 'PkgTool.Core.dll']
run(pkgtool + ['sfo_new', sfo])
entries = {
    'APP_TYPE': ('Integer', 4, '1'), 'APP_VER': ('Utf8', 8, args.app_version), 'ATTRIBUTE': ('Integer', 4, '0'),
    'CATEGORY': ('Utf8', 4, 'gde'), 'FORMAT': ('Utf8', 4, 'obs'), 'CONTENT_ID': ('Utf8', 48, CONTENT_ID),
    'DOWNLOAD_DATA_SIZE': ('Integer', 4, '0'), 'SYSTEM_VER': ('Integer', 4, '1020'),
    'TITLE': ('Utf8', 128, TITLE), 'TITLE_ID': ('Utf8', 12, TITLE_ID), 'VERSION': ('Utf8', 8, '01.00'),
}
for key, (kind, maximum, value) in entries.items():
    run(pkgtool + ['sfo_setentry', sfo, key, '--type', kind, '--maxsize', str(maximum), '--value', value])
run([BIN / 'create-gp4.exe', '-out', 'pkg.gp4', '--content-id=' + CONTENT_ID, '--files', ' '.join(files)])
# create-gp4's fixed directory template has no lang/ or glyphs/: declare them before the package is built.
gp4 = (stage / 'pkg.gp4').read_text(encoding='utf-8')
extra_dirs = ''.join(f'<dir targ_name="{name}" />' for name in ('lang', 'glyphs', 'music'))
gp4 = gp4.replace('<rootdir>', '<rootdir>' + extra_dirs, 1)
(stage / 'pkg.gp4').write_text(gp4, encoding='utf-8')
run(pkgtool + ['pkg_build', 'pkg.gp4', output])
package = output / (CONTENT_ID + '.pkg')
if not package.is_file() or package.read_bytes()[:4] != b'\x7fCNT':
    raise SystemExit('Package output missing or invalid header.')
record = {'status': 'compiled and packaged; hardware untested', 'title_id': TITLE_ID, 'content_id': CONTENT_ID,
          'game_data_included': False, 'sony_graphics_modules_included': bool(args.bundle_sprx)}
for label, path in (('elf', ELF), ('package', package)):
    record[label] = {'path': str(path), 'bytes': path.stat().st_size,
                     'sha256': hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()}
(output / 'build-evidence.json').write_text(json.dumps(record, indent=2) + '\n')
print('Package:', package)
