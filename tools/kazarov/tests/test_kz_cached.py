"""kz-cached: runs only when the inputs or the command line change."""

import os
import subprocess
import sys
from pathlib import Path

BIN = Path(__file__).resolve().parent.parent / 'bin'
KZ_CACHED = BIN / 'kz-cached'


def _cached(stamp, inputs, *command):
    args = [sys.executable, str(KZ_CACHED), '--stamp', str(stamp)]
    if inputs:
        args += ['--input', *map(str, inputs)]
    return subprocess.run([*args, '--', *command], capture_output=True, text=True)


def _log_command(log, text='ran'):
    return [sys.executable, '-c',
            f'open({str(log)!r}, "a").write({text!r} + "\\n")']


def test_runs_once_then_skips_until_an_input_changes(tmp_path):
    log = tmp_path / 'log'
    stamp = tmp_path / 'cache' / 'dcdata.stamp'
    data = tmp_path / 'disc.iso'
    data.write_bytes(b'abc')

    assert _cached(stamp, [data], *_log_command(log)).returncode == 0
    assert _cached(stamp, [data], *_log_command(log)).returncode == 0
    assert log.read_text().count('ran') == 1

    data.write_bytes(b'abcd')
    os.utime(data, (2_000_000_000, 2_000_000_000))
    assert _cached(stamp, [data], *_log_command(log)).returncode == 0
    assert log.read_text().count('ran') == 2


def test_a_changed_command_line_runs_again(tmp_path):
    log = tmp_path / 'log'
    stamp = tmp_path / 'c.stamp'
    data = tmp_path / 'in.bin'
    data.write_bytes(b'x')
    assert _cached(stamp, [data], *_log_command(log, 'one')).returncode == 0
    assert _cached(stamp, [data], *_log_command(log, 'two')).returncode == 0
    assert log.read_text().splitlines() == ['one', 'two']


def test_failure_passes_the_code_through_and_leaves_no_stamp(tmp_path):
    stamp = tmp_path / 'f.stamp'
    data = tmp_path / 'in.bin'
    data.write_bytes(b'x')
    result = _cached(stamp, [data], sys.executable, '-c', 'import sys; sys.exit(4)')
    assert result.returncode == 4
    assert not stamp.exists()


def test_stamp_is_kept_after_a_failed_rerun(tmp_path):
    log = tmp_path / 'log'
    stamp = tmp_path / 'g.stamp'
    data = tmp_path / 'in.bin'
    data.write_bytes(b'x')
    assert _cached(stamp, [data], *_log_command(log)).returncode == 0
    assert stamp.exists()

    data.write_bytes(b'xy')
    failing = [sys.executable, '-c', 'import sys; sys.exit(2)']
    assert _cached(stamp, [data], *failing).returncode == 2
    assert not stamp.exists()


def test_then_exec_runs_the_program_only_when_the_stamp_is_current(tmp_path):
    src = tmp_path / "in.txt"
    src.write_text("a")
    stamp = tmp_path / "stamp.json"
    out = tmp_path / "out.txt"
    base = [sys.executable, str(KZ_CACHED), "--stamp", str(stamp), "--input", str(src)]
    extract = ["sh", "-c", "true"]
    program = ["sh", "-c", f"echo ran > {out}"]
    stale = subprocess.run(base + ["--then-exec", "--"] + extract + ["--"] + program,
                           capture_output=True, text=True)
    assert stale.returncode != 0 and not out.exists()
    assert subprocess.run(base + ["--"] + extract).returncode == 0
    assert subprocess.run(base + ["--then-exec", "--"] + extract + ["--"] + program).returncode == 0
    assert out.read_text() == "ran\n"
