"""kz-split-stamp: a re-split that changes nothing keeps mtimes and the stamp."""

import os
import subprocess
import sys
from pathlib import Path

BIN = Path(__file__).resolve().parent.parent / 'bin'
KZ_SPLIT_STAMP = BIN / 'kz-split-stamp'
OLD = 1_500_000_000


def _run(*args):
    return subprocess.run([sys.executable, str(KZ_SPLIT_STAMP), *args],
                          capture_output=True, text=True)


def _set_mtime(path, seconds):
    os.utime(path, (seconds, seconds))
    return path.stat().st_mtime_ns


def _split(asm):
    (asm / 'sub').mkdir(parents=True, exist_ok=True)
    (asm / 'main.s').write_text('nop\n')
    (asm / 'sub' / 'data.s').write_text('.word 1\n')
    _set_mtime(asm / 'main.s', OLD)
    _set_mtime(asm / 'sub' / 'data.s', OLD)


def test_unchanged_resplit_keeps_mtimes_and_stamp(tmp_path):
    asm = tmp_path / 'asm'
    snap = tmp_path / 'asm.snap'
    stamp = tmp_path / 'split.stamp'
    _split(asm)

    assert _run('snapshot', str(asm), str(snap)).returncode == 0
    assert _run('restore', str(asm), str(snap), str(stamp)).returncode == 0
    stamp_mtime = _set_mtime(stamp, OLD)
    assert stamp.read_text().strip()

    # A re-split rewrites the same bytes, so the files look new.
    (asm / 'main.s').write_text('nop\n')
    (asm / 'sub' / 'data.s').write_text('.word 1\n')
    _set_mtime(asm / 'main.s', OLD + 500)
    _set_mtime(asm / 'sub' / 'data.s', OLD + 500)

    result = _run('restore', str(asm), str(snap), str(stamp))
    assert result.returncode == 0, result.stderr
    assert (asm / 'main.s').stat().st_mtime_ns == OLD * 10 ** 9
    assert (asm / 'sub' / 'data.s').stat().st_mtime_ns == OLD * 10 ** 9
    assert stamp.stat().st_mtime_ns == stamp_mtime


def test_changed_file_keeps_its_new_mtime_and_rewrites_the_stamp(tmp_path):
    asm = tmp_path / 'asm'
    snap = tmp_path / 'asm.snap'
    stamp = tmp_path / 'split.stamp'
    _split(asm)
    assert _run('snapshot', str(asm), str(snap)).returncode == 0
    assert _run('restore', str(asm), str(snap), str(stamp)).returncode == 0
    old_stamp = stamp.read_text()

    assert _run('snapshot', str(asm), str(snap)).returncode == 0
    (asm / 'sub' / 'data.s').write_text('.word 2\n')
    new_mtime = _set_mtime(asm / 'sub' / 'data.s', OLD + 500)
    (asm / 'main.s').write_text('nop\n')
    _set_mtime(asm / 'main.s', OLD + 500)

    assert _run('restore', str(asm), str(snap), str(stamp)).returncode == 0
    assert (asm / 'sub' / 'data.s').stat().st_mtime_ns == new_mtime
    assert (asm / 'main.s').stat().st_mtime_ns == (OLD * 10 ** 9)
    assert stamp.read_text() != old_stamp


def test_missing_directory_and_snapshot_give_an_empty_stamp(tmp_path):
    stamp = tmp_path / 'nested' / 'split.stamp'
    snap = tmp_path / 'none.snap'
    assert _run('snapshot', str(tmp_path / 'absent'), str(snap)).returncode == 0
    assert _run('restore', str(tmp_path / 'absent'), str(snap), str(stamp)).returncode == 0
    assert stamp.read_text().strip()
