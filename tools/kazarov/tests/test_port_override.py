"""port_override: a port's PORT_OVERRIDE definitions match ps2/src units, with or without --long-is-64.

Each case writes a port/src unit and a ps2/src unit under tmp_path and runs the real CLI.
"""

import importlib.util
import os
import subprocess
import sys
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parent.parent / 'scripts' / 'port_override.py'
MARKER = 'g_retail_marker'


def run(*args):
    return subprocess.run([sys.executable, str(SCRIPT), *map(str, args)], capture_output=True, text=True)


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)


def scenario(tmp_path, port_params, ps2_params):
    port = tmp_path / 'port' / 'src'
    write(port / 'a.cpp', f'PORT_OVERRIDE void A::F({port_params}) {{ }}\n')
    overrides = tmp_path / 'overrides.txt'
    listed = run('list', port, '-o', overrides)
    assert listed.returncode == 0, listed.stderr

    ps2 = tmp_path / 'ps2' / 'src'
    write(ps2 / 'a.cpp', f'void A::F({ps2_params}) {{ {MARKER} = 1; }}\n')
    stubs = tmp_path / 'stubs'
    stubs.mkdir()
    return overrides, ps2, stubs


def check(tmp_path, overrides, ps2, stubs, *flags):
    return run('check', '--overrides', overrides, '--stubs', stubs, *flags, ps2, '-o', tmp_path / 'checked')


def strip(tmp_path, overrides, ps2, *flags):
    out = tmp_path / 'stripped.cpp'
    result = run('strip', '--overrides', overrides, *flags, ps2 / 'a.cpp', out)
    assert result.returncode == 0, result.stderr
    return out.read_text(encoding='utf-8')


def test_long_long_port_does_not_match_long_without_option(tmp_path):
    overrides, ps2, stubs = scenario(tmp_path, 'unsigned long long x', 'unsigned long x')

    checked = check(tmp_path, overrides, ps2, stubs)
    assert checked.returncode != 0
    assert 'no ps2/src unit defines it under that signature' in checked.stderr

    assert MARKER in strip(tmp_path, overrides, ps2)


def test_long_long_port_matches_long_with_option(tmp_path):
    overrides, ps2, stubs = scenario(tmp_path, 'unsigned long long x', 'unsigned long x')

    checked = check(tmp_path, overrides, ps2, stubs, '--long-is-64')
    assert checked.returncode == 0, checked.stderr
    assert (tmp_path / 'checked').exists()

    assert MARKER not in strip(tmp_path, overrides, ps2, '--long-is-64')


@pytest.mark.parametrize('ps2_params', [
    'long x',
    'unsigned long x',
    'long int x',
    'const unsigned long *p',
])
def test_long_spellings_in_ps2_match_long_long_with_option(tmp_path, ps2_params):
    port_params = ps2_params.replace('long', 'long long', 1)
    overrides, ps2, stubs = scenario(tmp_path, port_params, ps2_params)

    assert check(tmp_path, overrides, ps2, stubs, '--long-is-64').returncode == 0
    assert MARKER not in strip(tmp_path, overrides, ps2, '--long-is-64')


@pytest.mark.parametrize('port_params, ps2_params', [
    ('int x', 'long x'),
    ('long long x', 'int x'),
])
def test_int_and_long_still_differ_with_option(tmp_path, port_params, ps2_params):
    overrides, ps2, stubs = scenario(tmp_path, port_params, ps2_params)

    checked = check(tmp_path, overrides, ps2, stubs, '--long-is-64')
    assert checked.returncode != 0
    assert 'no ps2/src unit defines it under that signature' in checked.stderr

    assert MARKER in strip(tmp_path, overrides, ps2, '--long-is-64')


@pytest.mark.parametrize('flags', [[], ['--long-is-64']])
def test_same_int_signature_matches_in_both_modes(tmp_path, flags):
    overrides, ps2, stubs = scenario(tmp_path, 'int x', 'int x')

    assert check(tmp_path, overrides, ps2, stubs, *flags).returncode == 0
    assert MARKER not in strip(tmp_path, overrides, ps2, *flags)


F_MARK, G_MARK, K_MARK = 'f_retail_marker', 'g_retail_marker', 'k_retail_marker'


def split_fixture(tmp_path):
    port = tmp_path / 'port' / 'src'
    write(port / 'a.cpp', 'PORT_OVERRIDE void F(int x) { }\n')
    write(port / 'b.cpp', 'PORT_OVERRIDE void G(int y) { }\n')
    overrides = tmp_path / 'overrides.txt'
    assert run('list', port, '-o', overrides).returncode == 0

    ps2 = tmp_path / 'ps2' / 'src'
    write(ps2 / 'a.cpp', f'void F(int x) {{ {F_MARK} = 1; }}\nvoid G(int y) {{ {G_MARK} = 1; }}\n')
    write(ps2 / 'b.cpp', f'void G(int y) {{ {G_MARK} = 1; }}\nvoid K(int z) {{ {K_MARK} = 1; }}\n')
    write(ps2 / 'sub' / 'c.cpp', f'void K(int z) {{ {K_MARK} = 1; }}\n')
    stubs = tmp_path / 'stubs'
    stubs.mkdir()
    return overrides, ps2, stubs


def check_args(tmp_path, overrides, ps2, stubs):
    return ['check', '--overrides', overrides, '--stubs', stubs, '--split', tmp_path / 'split',
            '--cache', tmp_path / 'cache.json', ps2, '-o', tmp_path / 'checked']


def strip_to(tmp_path, name, overrides, source, *flags):
    out = tmp_path / 'out' / name
    result = run('strip', '--overrides', overrides, *flags, source, out)
    assert result.returncode == 0, result.stderr
    return out.read_bytes()


def split_text(tmp_path, unit):
    return (tmp_path / 'split' / f'{unit}.txt').read_text(encoding='utf-8')


def load_script(monkeypatch):
    monkeypatch.setattr(sys, 'dont_write_bytecode', True)
    spec = importlib.util.spec_from_file_location('port_override', SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def count_parses(monkeypatch, module):
    parses = []
    parse = module.overridden

    def counted(*args, **kwargs):
        parses.append(1)
        return parse(*args, **kwargs)

    monkeypatch.setattr(module, 'overridden', counted)
    return parses


def check_in_process(monkeypatch, module, *args):
    monkeypatch.setattr(sys, 'argv', ['port_override.py', *map(str, args)])
    try:
        return module.main()
    except SystemExit as exit:
        return exit.code


def test_split_files_hold_the_entries_each_unit_matches(tmp_path):
    overrides, ps2, stubs = split_fixture(tmp_path)
    write(overrides, 'G(int)\nF(int)\n')

    checked = run(*check_args(tmp_path, overrides, ps2, stubs))
    assert checked.returncode == 0, checked.stderr

    assert split_text(tmp_path, 'a.cpp') == 'G(int)\nF(int)\n'
    assert split_text(tmp_path, 'b.cpp') == 'G(int)\n'
    assert split_text(tmp_path, 'sub/c.cpp') == ''
    for unit in ('a.cpp', 'b.cpp', 'sub/c.cpp'):
        full = strip_to(tmp_path, f'full-{unit}', overrides, ps2 / unit)
        part = strip_to(tmp_path, f'split-{unit}', tmp_path / 'split' / f'{unit}.txt', ps2 / unit)
        assert full == part
    stripped = strip_to(tmp_path, 'a', overrides, ps2 / 'a.cpp')
    assert F_MARK.encode() not in stripped and G_MARK.encode() not in stripped


def test_split_file_of_a_renamed_definition_strips_identically(tmp_path):
    port = tmp_path / 'port' / 'src'
    write(port / 'r.cpp', 'PORT_OVERRIDE void NewName(int x) { }\n')
    overrides = tmp_path / 'overrides.txt'
    assert run('list', port, '-o', overrides).returncode == 0
    ps2 = tmp_path / 'ps2' / 'src'
    write(ps2 / 'r.cpp', f'void OldName(int x) {{ {F_MARK} = 1; }}\n')
    stubs = tmp_path / 'stubs'
    write(stubs / 'r.hpp', '#define OldName NewName\n')

    checked = run(*check_args(tmp_path, overrides, ps2, stubs))
    assert checked.returncode == 0, checked.stderr
    assert split_text(tmp_path, 'r.cpp') == 'NewName(int)\n'

    full = strip_to(tmp_path, 'full', overrides, ps2 / 'r.cpp', '--stubs', stubs / 'r.hpp')
    part = strip_to(tmp_path, 'part', tmp_path / 'split' / 'r.cpp.txt', ps2 / 'r.cpp', '--stubs', stubs / 'r.hpp')
    assert full == part
    assert F_MARK.encode() not in full


def test_cache_hit_skips_parsing_and_gives_the_same_result(tmp_path, monkeypatch):
    module = load_script(monkeypatch)
    parses = count_parses(monkeypatch, module)
    overrides, ps2, stubs = split_fixture(tmp_path)
    args = check_args(tmp_path, overrides, ps2, stubs)

    assert check_in_process(monkeypatch, module, *args) == 0
    assert len(parses) == 3
    cold = {unit: split_text(tmp_path, unit) for unit in ('a.cpp', 'b.cpp', 'sub/c.cpp')}
    checked = (tmp_path / 'checked').read_bytes()

    parses.clear()
    assert check_in_process(monkeypatch, module, *args) == 0
    assert parses == []
    assert {unit: split_text(tmp_path, unit) for unit in cold} == cold
    assert (tmp_path / 'checked').read_bytes() == checked


@pytest.mark.parametrize('change, reparsed', [('unit', 1), ('stub', 1), ('overrides', 3)])
def test_cache_entry_is_invalidated_by_what_it_depends_on(tmp_path, monkeypatch, change, reparsed):
    module = load_script(monkeypatch)
    parses = count_parses(monkeypatch, module)
    overrides, ps2, stubs = split_fixture(tmp_path)
    args = check_args(tmp_path, overrides, ps2, stubs)
    assert check_in_process(monkeypatch, module, *args) == 0
    parses.clear()

    if change == 'unit':
        write(ps2 / 'b.cpp', f'void G(int y) {{ {G_MARK} = 2; }}\nvoid K(int z) {{ {K_MARK} = 1; }}\n')
    elif change == 'stub':
        write(stubs / 'b.hpp', '#define Unused Other\n')
    else:
        write(overrides, 'G(int)\nF(int)\n')

    assert check_in_process(monkeypatch, module, *args) == 0
    assert len(parses) == reparsed


def test_unmatched_override_fails_as_before_with_the_new_flags(tmp_path):
    overrides, ps2, stubs = split_fixture(tmp_path)
    write(overrides, 'F(int)\nG(int)\nNope(int)\n')

    plain = run('check', '--overrides', overrides, '--stubs', stubs, ps2, '-o', tmp_path / 'plain')
    assert plain.returncode != 0
    assert 'no ps2/src unit defines it under that signature' in plain.stderr

    for _ in range(2):
        flagged = run(*check_args(tmp_path, overrides, ps2, stubs))
        assert flagged.returncode == plain.returncode
        assert flagged.stderr == plain.stderr
    assert not (tmp_path / 'checked').exists()
    assert not (tmp_path / 'split').exists()


def test_parse_error_fails_as_before_and_is_reproduced_from_the_cache(tmp_path):
    overrides, ps2, stubs = split_fixture(tmp_path)
    write(ps2 / 'bad.cpp', 'int z = 1 \\\n;\n')

    plain = run('check', '--overrides', overrides, '--stubs', stubs, ps2, '-o', tmp_path / 'plain')
    assert plain.returncode != 0
    assert 'a line continued with a backslash' in plain.stderr

    for _ in range(2):
        flagged = run(*check_args(tmp_path, overrides, ps2, stubs))
        assert flagged.returncode == plain.returncode
        assert flagged.stderr == plain.stderr
    assert not (tmp_path / 'checked').exists()
    assert not (tmp_path / 'split').exists()


def test_outputs_keep_their_mtime_when_unchanged(tmp_path):
    overrides, ps2, stubs = split_fixture(tmp_path)
    args = check_args(tmp_path, overrides, ps2, stubs)
    assert run(*args).returncode == 0

    paths = [tmp_path / 'checked', tmp_path / 'cache.json', *(tmp_path / 'split').rglob('*.txt')]
    stamp = 10 ** 18
    for path in paths:
        os.utime(path, ns=(stamp, stamp))

    assert run(*args).returncode == 0
    for path in paths:
        assert path.stat().st_mtime_ns == stamp, path
