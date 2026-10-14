"""kz-ffmpeg: the LGPL configure line, the build stamp, and the refusals."""

import hashlib
import io
import json
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

import pytest

BIN = Path(__file__).resolve().parent.parent / 'bin'
KZ_FFMPEG = Path(__file__).resolve().parent.parent / 'image' / 'kz-ffmpeg'
KZ_FFMPEG_LAUNCHER = BIN / 'kz-ffmpeg'
LICENSE_LINE = 'License: LGPL version 2.1 or later'
GPL_LICENSE_LINE = 'License: GPL version 2 or later'
FLAGS = [
    '--disable-everything',
    '--disable-programs',
    '--disable-doc',
    '--disable-network',
    '--disable-autodetect',
    '--disable-avformat',
    '--disable-avfilter',
    '--disable-swscale',
    '--disable-swresample',
    '--disable-avdevice',
    '--enable-decoder=mpeg2video',
    '--enable-parser=mpegvideo',
    '--enable-pic',
    '--disable-shared',
    '--enable-static',
]
GPL_FLAGS = ['--enable-gpl', '--enable-nonfree', '--enable-version3']

CONFIGURE_SH = '''#!/bin/sh
echo configured >> "$KZ_FAKE_FFMPEG_LOG"
echo "@LICENSE@"
for arg in "$@"; do
  case "$arg" in
    --prefix=*) prefix=${arg#--prefix=} ;;
  esac
done
printf 'all:\\n\\t@true\\n\\ninstall:\\n\\tmkdir -p %s/lib %s/include/libavcodec\\n\\ttouch %s/lib/libavcodec.a %s/lib/libavutil.a %s/include/libavcodec/avcodec.h\\n' "$prefix" "$prefix" "$prefix" "$prefix" "$prefix" > Makefile
'''

needs_make = pytest.mark.skipif(os.name == 'nt', reason='needs sh and make')
requires_make = pytest.mark.skipif(shutil.which('make') is None, reason='needs make')


def _ff(*args, log=None):
    env = dict(os.environ, KZ_FAKE_FFMPEG_LOG=str(log)) if log is not None else None
    return subprocess.run([sys.executable, str(KZ_FFMPEG), *map(str, args)],
                          capture_output=True, text=True, env=env)


def _print_configure():
    return subprocess.run([sys.executable, str(KZ_FFMPEG), '--print-configure'],
                          capture_output=True, text=True).stdout


def _fake_tarball(tmp_path, license_line):
    script = CONFIGURE_SH.replace('@LICENSE@', license_line).encode()
    path = tmp_path / 'ffmpeg-7.1.1.tar.xz'
    with tarfile.open(path, 'w:xz') as tf:
        info = tarfile.TarInfo('ffmpeg-7.1.1/configure')
        info.size = len(script)
        info.mode = 0o755
        tf.addfile(info, io.BytesIO(script))
    return path, hashlib.sha256(path.read_bytes()).hexdigest()


def test_configure_line_is_lgpl():
    result = subprocess.run([sys.executable, str(KZ_FFMPEG), '--print-configure'],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    tokens = shlex.split(result.stdout)
    assert tokens[0] == 'configure'
    for flag in FLAGS:
        assert flag in tokens
    for flag in GPL_FLAGS:
        assert flag not in tokens


def test_the_launcher_runs_the_image_script():
    launched = subprocess.run([sys.executable, str(KZ_FFMPEG_LAUNCHER), '--print-configure'],
                              capture_output=True, text=True)
    direct = subprocess.run([sys.executable, str(KZ_FFMPEG), '--print-configure'],
                            capture_output=True, text=True)
    assert launched.returncode == 0
    assert launched.stdout == direct.stdout


@needs_make
@requires_make
def test_build_writes_a_stamp_that_round_trips(tmp_path):
    tarball, sha = _fake_tarball(tmp_path, LICENSE_LINE)
    out, log = tmp_path / 'ff', tmp_path / 'fake.log'

    built = _ff('--out', out, '--tarball', tarball, '--sha256', sha, log=log)
    assert built.returncode == 0, built.stderr

    stamp = json.loads((out / 'kz-ffmpeg.json').read_text())
    assert stamp['version'] == '7.1.1'
    assert stamp['sha256'] == sha
    assert stamp['configure'] == shlex.split(_print_configure())[1:]
    assert stamp['license'] == LICENSE_LINE

    assert _ff('--out', out, '--sha256', sha, '--check').returncode == 0

    again = _ff('--out', out, '--tarball', tarball, '--sha256', sha, log=log)
    assert again.returncode == 0
    assert 'up to date' in again.stdout
    assert log.read_text().splitlines() == ['configured']


@needs_make
@requires_make
def test_a_changed_stamp_rebuilds(tmp_path):
    tarball, sha = _fake_tarball(tmp_path, LICENSE_LINE)
    out, log = tmp_path / 'ff', tmp_path / 'fake.log'
    build = ['--out', out, '--tarball', tarball, '--sha256', sha]
    assert _ff(*build, log=log).returncode == 0

    stamp_path = out / 'kz-ffmpeg.json'
    good = json.loads(stamp_path.read_text())
    tampered = dict(good, configure=[*good['configure'], '--enable-gpl'])
    stamp_path.write_text(json.dumps(tampered))

    assert _ff('--out', out, '--sha256', sha, '--check').returncode == 1
    assert _ff(*build, log=log).returncode == 0
    assert log.read_text().splitlines() == ['configured', 'configured']
    assert json.loads(stamp_path.read_text()) == good


@needs_make
@requires_make
def test_a_wrong_checksum_is_refused(tmp_path):
    tarball, _ = _fake_tarball(tmp_path, LICENSE_LINE)
    out, log = tmp_path / 'ff', tmp_path / 'fake.log'

    result = _ff('--out', out, '--tarball', tarball, '--sha256', '0' * 64, log=log)

    assert result.returncode != 0
    assert not (out / 'kz-ffmpeg.json').exists()
    assert not log.exists() or 'configured' not in log.read_text()


@needs_make
@requires_make
def test_a_gpl_configure_is_refused(tmp_path):
    tarball, sha = _fake_tarball(tmp_path, GPL_LICENSE_LINE)
    out, log = tmp_path / 'ff', tmp_path / 'fake.log'

    result = _ff('--out', out, '--tarball', tarball, '--sha256', sha, log=log)

    assert result.returncode != 0
    assert not (out / 'kz-ffmpeg.json').exists()


@pytest.mark.skipif(not Path('/opt/kazarov/ffmpeg/kz-ffmpeg.json').exists(),
                    reason='runs in the Kazarov image')
def test_the_image_build_is_lgpl():
    out = Path('/opt/kazarov/ffmpeg')
    result = subprocess.run([sys.executable, str(KZ_FFMPEG), '--check', '--out', str(out)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    lines = (out / 'configure.log').read_text().splitlines()
    assert LICENSE_LINE in [line.rstrip() for line in lines]
