"""host/kazarov.ps1 dispatch and container argv, run under pwsh against a scratch project.

A fake docker records its argv, and a fake Windows host script records its argv. Skipped
when no pwsh is found: KZ_PWSH names one, otherwise pwsh is taken from PATH.
"""

import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
KZ = HERE.parent
SCRIPT = KZ / "host" / "kazarov.ps1"
IMAGE_ID = "sha256:" + "ab" * 32

CONF = """\
KZ_PROJECT=scratch
KZ_DEFAULT_BUILD=ps2
KZ_DEFAULT_BUILD_WINDOWS=win
KZ_DEFAULT_RUN=ps2
KZ_DEFAULT_RUN_WINDOWS=win
KZ_WINDOWS_HOST_SCRIPT=fake_host.ps1
KZ_WINDOWS_CONFIGS=(win)
KZ_CONTAINER_CONFIGS="ps2"
"""

FAKE_HOST = """\
[IO.File]::WriteAllText((Join-Path $PSScriptRoot 'windows-argv.json'), (ConvertTo-Json -InputObject @($args) -Compress))
"""

FAKE_DOCKER = """\
#!/usr/bin/env python3
import json
import os
import sys

with open(os.environ["FAKE_DOCKER_LOG"], "a", encoding="utf-8") as log:
    log.write(json.dumps(sys.argv[1:]) + "\\n")
if sys.argv[1:3] == ["image", "inspect"]:
    print("%s")
if sys.argv[1:2] == ["run"] and os.environ.get("FAKE_DESCRIPTOR"):
    # Writes the run descriptor where the container would, inside the mounted project.
    args = sys.argv[1:]
    out = next(a.split("=", 1)[1] for a in args if a.startswith("KZ_RUN_OUT="))
    mount = args[args.index("-v") + 1].rsplit(":/work/", 1)[0]
    with open(os.path.join(mount, out), "w", encoding="utf-8") as desc:
        desc.write(os.environ["FAKE_DESCRIPTOR"] + "\\n")
sys.exit(0)
""" % IMAGE_ID


def find_pwsh():
    configured = os.environ.get("KZ_PWSH")
    if configured:
        return configured if Path(configured).is_file() else None
    return shutil.which("pwsh")


@pytest.fixture
def pwsh():
    found = find_pwsh()
    if found is None:
        pytest.skip("pwsh not found: set KZ_PWSH or put pwsh on PATH")
    return found


@pytest.fixture
def project(tmp_path):
    root = tmp_path / "my project"
    root.mkdir()
    (root / "kazarov.conf").write_text(CONF, encoding="utf-8")
    (root / "fake_host.ps1").write_text(FAKE_HOST, encoding="utf-8")
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    docker = bin_dir / "docker"
    docker.write_text(FAKE_DOCKER, encoding="utf-8")
    docker.chmod(0o755)
    return root, bin_dir


def run_kazarov(pwsh, project, *args, descriptor=None, conf=None):
    root, bin_dir = project
    log = bin_dir.parent / "docker.log"
    log.unlink(missing_ok=True)
    if conf is not None:
        (root / "kazarov.conf").write_text(conf, encoding="utf-8")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(bin_dir), str(Path(pwsh).resolve().parent), env["PATH"]])
    env["BUILDER"] = "docker"
    env["FAKE_DOCKER_LOG"] = str(log)
    env.pop("REBUILD_IMAGE", None)
    env.pop("FAKE_DESCRIPTOR", None)
    if descriptor is not None:
        env["FAKE_DESCRIPTOR"] = descriptor
    result = subprocess.run([pwsh, "-NoProfile", "-File", str(SCRIPT), str(root), *args],
                            capture_output=True, text=True, env=env, cwd=str(bin_dir))
    docker_calls = []
    if log.exists():
        docker_calls = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
    windows = root / "windows-argv.json"
    windows_argv = json.loads(windows.read_text(encoding="utf-8")) if windows.exists() else None
    if windows.exists():
        windows.unlink()
    return result, docker_calls, windows_argv


def container_run(calls):
    runs = [call for call in calls if call[:1] == ["run"]]
    assert len(runs) == 1, calls
    return runs[0]


def test_no_config_uses_the_windows_default(pwsh, project):
    result, calls, windows_argv = run_kazarov(pwsh, project, "build")
    assert result.returncode == 0, result.stderr
    assert windows_argv == ["build", "win"]
    assert calls == []


def test_listed_container_config_runs_in_the_container(pwsh, project):
    result, calls, windows_argv = run_kazarov(pwsh, project, "build", "ps2")
    assert result.returncode == 0, result.stderr
    assert windows_argv is None
    run = container_run(calls)
    assert run[-4:] == ["tools/kazarov/bin/kazarov", "--inside", "build", "ps2"]
    assert IMAGE_ID in run
    assert f"{project[0]}:/work/scratch" in run


def test_unlisted_argument_is_forwarded_to_the_default_config(pwsh, project):
    result, calls, windows_argv = run_kazarov(pwsh, project, "build", "foo")
    assert result.returncode == 0, result.stderr
    assert windows_argv == ["build", "win", "foo"]
    assert calls == []


def test_dash_argument_is_forwarded_to_the_default_config(pwsh, project):
    result, calls, windows_argv = run_kazarov(pwsh, project, "build", "-x")
    assert result.returncode == 0, result.stderr
    assert windows_argv == ["build", "win", "-x"]
    assert calls == []


def test_argument_with_metacharacters_reaches_docker_intact(pwsh, project):
    tricky = 'foo&echo X&rem "q" bar baz'
    result, calls, windows_argv = run_kazarov(pwsh, project, "build", "ps2", tricky)
    assert result.returncode == 0, result.stderr
    assert windows_argv is None
    run = container_run(calls)
    assert run[-6:] == [IMAGE_ID, "tools/kazarov/bin/kazarov", "--inside", "build", "ps2", tricky]


def test_container_default_is_passed_in_the_config_slot(pwsh, project):
    conf = CONF.replace("KZ_DEFAULT_BUILD_WINDOWS=win", "KZ_DEFAULT_BUILD_WINDOWS=")
    result, calls, windows_argv = run_kazarov(pwsh, project, "build", "-x", conf=conf)
    assert result.returncode == 0, result.stderr
    assert windows_argv is None
    run = container_run(calls)
    assert run[-5:] == ["tools/kazarov/bin/kazarov", "--inside", "build", "ps2", "-x"]


def test_exe_launch_passes_arguments_as_separate_words(pwsh, project):
    root, _ = project
    exe = root / "game.sh"
    exe.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$(dirname "$0")/args.txt"\n', encoding="utf-8")
    exe.chmod(0o755)
    result, calls, windows_argv = run_kazarov(pwsh, project, "run", "ps2", "a b", 'c"d',
                                              descriptor="exe:game.sh")
    assert result.returncode == 0, result.stderr
    assert (root / "args.txt").read_text(encoding="utf-8").splitlines() == ["a b", 'c"d']


def test_iso_path_with_spaces_stays_one_argument(pwsh, project):
    root, _ = project
    iso_dir = root / "my images"
    iso_dir.mkdir()
    (iso_dir / "game.iso").write_bytes(b"")
    recorder = project[1].parent / "pcsx2.sh"
    recorder.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$(dirname "$0")/pcsx2-args.txt"\n', encoding="utf-8")
    recorder.chmod(0o755)
    env_before = os.environ.get("PCSX2")
    os.environ["PCSX2"] = str(recorder)
    try:
        result, calls, windows_argv = run_kazarov(pwsh, project, "run", "ps2",
                                                  descriptor="iso:my images/game.iso")
    finally:
        if env_before is None:
            os.environ.pop("PCSX2", None)
        else:
            os.environ["PCSX2"] = env_before
    assert result.returncode == 0, result.stderr
    out = project[1].parent / "pcsx2-args.txt"
    deadline = time.monotonic() + 10
    while not out.exists() and time.monotonic() < deadline:
        time.sleep(0.05)
    assert out.read_text(encoding="utf-8").splitlines() == ["-fastboot", "--", str(iso_dir / "game.iso")]


QUOTE_FILE = KZ / "host" / "kazarov-quote.ps1"

# Each case: the argument, and what it must become on a command line.
QUOTE_CASES = [
    ("a b", '"a b"'),
    ('a"b', '"a\\"b"'),
    ('a\\"b', '"a\\\\\\"b"'),
    ("trailing\\", "trailing\\"),
    ("dir with space\\", '"dir with space\\\\"'),
    ("", '""'),
    ("&|<>", "&|<>"),
]


def split_command_line(line):
    """The rules CommandLineToArgvW applies to one command line, written out for the tests."""
    args, i, n = [], 0, len(line)
    while True:
        while i < n and line[i] in " \t":
            i += 1
        if i >= n:
            return args
        arg, quoted = [], False
        while i < n and (quoted or line[i] not in " \t"):
            backslashes = 0
            while i < n and line[i] == "\\":
                backslashes += 1
                i += 1
            if i < n and line[i] == '"':
                arg.append("\\" * (backslashes // 2))
                if backslashes % 2:
                    arg.append('"')
                elif quoted and i + 1 < n and line[i + 1] == '"':
                    arg.append('"')
                    i += 1
                else:
                    quoted = not quoted
                i += 1
            else:
                arg.append("\\" * backslashes)
                if i < n and (quoted or line[i] not in " \t"):
                    arg.append(line[i])
                    i += 1
        args.append("".join(arg))


def test_quote_parser_matches_known_command_line_results():
    assert split_command_line('"a\\\\\\"b"') == ['a\\"b']
    assert split_command_line('a\\\\ "x y" z') == ['a\\\\', "x y", "z"]
    assert split_command_line('""') == [""]


def test_argument_quoting_matches_command_line_rules(pwsh, tmp_path):
    script = tmp_path / "quote-check.ps1"
    script.write_text(
        f". '{QUOTE_FILE}'\n"
        "$cases = ConvertFrom-Json -InputObject $env:KZ_QUOTE_INPUT\n"
        "$out = @($cases | ForEach-Object { ConvertTo-WindowsArgument $_ })\n"
        "ConvertTo-Json -InputObject $out -Compress\n",
        encoding="utf-8")
    inputs = [arg for arg, _ in QUOTE_CASES]
    env = dict(os.environ, KZ_QUOTE_INPUT=json.dumps(inputs))
    result = subprocess.run([pwsh, "-NoProfile", "-File", str(script)],
                            capture_output=True, text=True, env=env)
    assert result.returncode == 0, result.stderr
    quoted = json.loads(result.stdout)
    assert quoted == [expected for _, expected in QUOTE_CASES]
    for (arg, _), text in zip(QUOTE_CASES, quoted):
        assert split_command_line(text) == [arg], (arg, text)
    # A full command line of several arguments reads back as the same list.
    line = " ".join(quoted)
    assert split_command_line(line) == inputs
