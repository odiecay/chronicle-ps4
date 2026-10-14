"""kz-lock: waiting, exit codes, signal forwarding."""

import os
import signal
import subprocess
import sys
import time
from pathlib import Path

BIN = Path(__file__).resolve().parent.parent / 'bin'
KZ_LOCK = BIN / 'kz-lock'


def _kz_lock(*args, **kwargs):
    return subprocess.Popen([sys.executable, str(KZ_LOCK), *args], **kwargs)


def test_runs_command_and_returns_its_exit_code(tmp_path):
    lock = tmp_path / 'a.lock'
    result = subprocess.run([sys.executable, str(KZ_LOCK), '--lock', str(lock), '--',
                             sys.executable, '-c', 'print("ran")'],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert result.stdout == 'ran\n'
    assert lock.exists()

    result = subprocess.run([sys.executable, str(KZ_LOCK), '--lock', str(lock), '--',
                             sys.executable, '-c', 'import sys; sys.exit(7)'])
    assert result.returncode == 7


def test_creates_parent_directories(tmp_path):
    lock = tmp_path / '.kazarov' / 'locks' / 'ps2-pal.lock'
    result = subprocess.run([sys.executable, str(KZ_LOCK), '--lock', str(lock), '--', 'true'])
    assert result.returncode == 0
    assert lock.is_file()


def test_second_holder_waits_and_prints_the_message_once(tmp_path):
    lock = tmp_path / 'held.lock'
    marker = tmp_path / 'released'
    holder = subprocess.Popen(
        [sys.executable, '-c',
         'import fcntl, os, sys, time\n'
         'fd = os.open(sys.argv[1], os.O_RDWR | os.O_CREAT)\n'
         'fcntl.flock(fd, fcntl.LOCK_EX)\n'
         'print("held", flush=True)\n'
         'time.sleep(1.0)\n'
         'open(sys.argv[2], "w").close()\n',
         str(lock), str(marker)],
        stdout=subprocess.PIPE, text=True)
    try:
        assert holder.stdout.readline().strip() == 'held'
        start = time.monotonic()
        result = subprocess.run(
            [sys.executable, str(KZ_LOCK), '--lock', str(lock), '--message', 'build dir busy', '--',
             sys.executable, '-c', 'import os, sys; sys.exit(0 if os.path.exists(sys.argv[1]) else 3)',
             str(marker)],
            capture_output=True, text=True)
        waited = time.monotonic() - start
    finally:
        holder.wait(timeout=10)

    assert result.returncode == 0, result.stderr
    assert waited >= 0.5
    assert result.stderr.count(f'kazarov: waiting for {lock}') == 1
    assert result.stderr.count('kazarov: build dir busy') == 1


def test_uncontended_lock_prints_nothing(tmp_path):
    result = subprocess.run([sys.executable, str(KZ_LOCK), '--lock', str(tmp_path / 'x.lock'),
                             '--message', 'busy', '--', 'true'], capture_output=True, text=True)
    assert result.returncode == 0
    assert 'waiting' not in result.stderr
    assert 'busy' not in result.stderr


def test_sigterm_is_forwarded_to_the_command(tmp_path):
    proc = subprocess.Popen(
        [sys.executable, str(KZ_LOCK), '--lock', str(tmp_path / 's.lock'), '--',
         sys.executable, '-c', 'import time; print("ready", flush=True); time.sleep(60)'],
        stdout=subprocess.PIPE, text=True)
    try:
        assert proc.stdout.readline().strip() == 'ready'
        proc.send_signal(signal.SIGTERM)
        code = proc.wait(timeout=10)
    finally:
        if proc.poll() is None:
            proc.kill()
    assert code == 128 + signal.SIGTERM


def _gone(pid):
    """True once the process has exited; a zombie left for init to reap counts as gone."""
    stat = Path('/proc') / str(pid) / 'stat'
    if stat.exists():
        return stat.read_text().rsplit(')', 1)[1].split()[0] == 'Z'
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    return False


def test_sigterm_reaches_the_commands_children(tmp_path):
    pid_file = tmp_path / 'grandchild.pid'
    child = ('import subprocess, sys\n'
             'g = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
             'open(sys.argv[1], "w").write(str(g.pid))\n'
             'print("ready", flush=True)\n'
             'g.wait()\n')
    proc = subprocess.Popen(
        [sys.executable, str(KZ_LOCK), '--lock', str(tmp_path / 'g.lock'), '--',
         sys.executable, '-c', child, str(pid_file)],
        stdout=subprocess.PIPE, text=True)
    try:
        assert proc.stdout.readline().strip() == 'ready'
        grandchild = int(pid_file.read_text())
        proc.send_signal(signal.SIGTERM)
        code = proc.wait(timeout=10)
    finally:
        if proc.poll() is None:
            proc.kill()
    assert code != 0
    deadline = time.monotonic() + 5
    while not _gone(grandchild) and time.monotonic() < deadline:
        time.sleep(0.05)
    assert _gone(grandchild)


def test_grandchild_left_behind_by_a_normal_exit_is_ended_before_unlock(tmp_path):
    pid_file = tmp_path / 'left.pid'
    child = ('import subprocess, sys\n'
             'g = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])\n'
             'open(sys.argv[1], "w").write(str(g.pid))\n')
    result = subprocess.run(
        [sys.executable, str(KZ_LOCK), '--lock', str(tmp_path / 'left.lock'), '--',
         sys.executable, '-c', child, str(pid_file)],
        timeout=30)
    assert result.returncode == 0
    assert _gone(int(pid_file.read_text()))


def test_grandchild_ignoring_sigterm_is_killed_after_the_wait(tmp_path):
    pid_file = tmp_path / 'stubborn.pid'
    ready = tmp_path / 'ready'
    grandchild = ('import signal, sys, time\n'
                  'signal.signal(signal.SIGTERM, signal.SIG_IGN)\n'
                  'open(sys.argv[1], "w").close()\n'
                  'time.sleep(60)\n')
    child = ('import os, subprocess, sys, time\n'
             'g = subprocess.Popen([sys.executable, "-c", sys.argv[2], sys.argv[3]])\n'
             'open(sys.argv[1], "w").write(str(g.pid))\n'
             'while not os.path.exists(sys.argv[3]):\n'
             '    time.sleep(0.01)\n')
    start = time.monotonic()
    result = subprocess.run(
        [sys.executable, str(KZ_LOCK), '--lock', str(tmp_path / 'stubborn.lock'), '--',
         sys.executable, '-c', child, str(pid_file), grandchild, str(ready)],
        timeout=30)
    waited = time.monotonic() - start
    assert result.returncode == 0
    assert waited >= 5.0
    assert _gone(int(pid_file.read_text()))


def _hold(lock, mode, seconds):
    """A kz-lock holder that prints 'ready' once it holds the lock, then sleeps."""
    args = ['--shared'] if mode == 'shared' else []
    return subprocess.Popen(
        [sys.executable, str(KZ_LOCK), *args, '--lock', str(lock), '--',
         sys.executable, '-c', f'import time; print("ready", flush=True); time.sleep({seconds})'],
        stdout=subprocess.PIPE, text=True)


def _run_timed(lock, mode):
    args = ['--shared'] if mode == 'shared' else []
    start = time.monotonic()
    result = subprocess.run(
        [sys.executable, str(KZ_LOCK), *args, '--lock', str(lock), '--', 'true'],
        capture_output=True, text=True)
    return result, time.monotonic() - start


def test_two_shared_holders_run_concurrently(tmp_path):
    lock = tmp_path / 'shared.lock'
    holder = _hold(lock, 'shared', 3)
    try:
        assert holder.stdout.readline().strip() == 'ready'
        result, waited = _run_timed(lock, 'shared')
    finally:
        holder.wait(timeout=10)
    assert result.returncode == 0, result.stderr
    assert waited < 1.5
    assert 'waiting' not in result.stderr


def test_exclusive_waits_for_shared_holders(tmp_path):
    lock = tmp_path / 'mixed.lock'
    holder = _hold(lock, 'shared', 1.0)
    try:
        assert holder.stdout.readline().strip() == 'ready'
        result, waited = _run_timed(lock, 'exclusive')
    finally:
        holder.wait(timeout=10)
    assert result.returncode == 0, result.stderr
    assert waited >= 0.5
    assert result.stderr.count(f'kazarov: waiting for {lock}') == 1


def test_shared_waits_for_an_exclusive_holder(tmp_path):
    lock = tmp_path / 'mixed2.lock'
    holder = _hold(lock, 'exclusive', 1.0)
    try:
        assert holder.stdout.readline().strip() == 'ready'
        result, waited = _run_timed(lock, 'shared')
    finally:
        holder.wait(timeout=10)
    assert result.returncode == 0, result.stderr
    assert waited >= 0.5
    assert result.stderr.count(f'kazarov: waiting for {lock}') == 1
