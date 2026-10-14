"""kz-extract: caching, checksum checks and the ISO 9660 reader.

Most cases run against a small ISO 9660 image that the test writes itself, so
they need no ISO tools. The last case runs against a genisoimage/mkisofs/
xorriso image when one of them is installed.
"""

import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

BIN = Path(__file__).resolve().parent.parent / 'bin'
KZ_EXTRACT = BIN / 'kz-extract'
SECTOR = 2048

TREE = {
    b'SCES_511.90;1': bytes(range(256)) * 20,
    b'SYSTEM.CNF;1': b'BOOT2 = cdrom0:\\SCES_511.90;1\n',
    b'DMMY.;1': b'',
    b'MOVIE': {
        b'E54001.PSS;1': b'\xAA' * 3000,
        b'OPEN1.PSS;1': b'\x55' * 17,
    },
}

EXPECTED_FILES = {
    'SCES_511.90': TREE[b'SCES_511.90;1'],
    'SYSTEM.CNF': TREE[b'SYSTEM.CNF;1'],
    'DMMY': b'',
    'MOVIE/E54001.PSS': b'\xAA' * 3000,
    'MOVIE/OPEN1.PSS': b'\x55' * 17,
}


def _record_len(name):
    return 33 + len(name) + (1 if len(name) % 2 == 0 else 0)


def _record(name, extent, size, is_dir):
    length = _record_len(name)
    raw = bytearray(length)
    raw[0] = length
    struct.pack_into('<I', raw, 2, extent)
    struct.pack_into('>I', raw, 6, extent)
    struct.pack_into('<I', raw, 10, size)
    struct.pack_into('>I', raw, 14, size)
    raw[25] = 2 if is_dir else 0
    struct.pack_into('<H', raw, 28, 1)
    struct.pack_into('>H', raw, 30, 1)
    raw[32] = len(name)
    raw[33:33 + len(name)] = name
    return bytes(raw)


def build_iso(path, tree):
    """Write an ISO 9660 image holding the nested dict of files (bytes) and directories (dict)."""
    image = bytearray(18 * SECTOR)
    cursor = [18]

    def alloc(nbytes):
        start = cursor[0]
        cursor[0] += max(1, -(-nbytes // SECTOR))
        return start

    def place(extent, data):
        end = (extent * SECTOR) + len(data)
        if len(image) < end:
            image.extend(b'\0' * (end - len(image)))
        image[extent * SECTOR:end] = data

    def write_dir(entries):
        size = 68 + sum(_record_len(name) for name in entries)
        extent = alloc(size)
        rows = [_record(b'\0', extent, size, True), _record(b'\1', 0, 0, True)]
        for name, value in entries.items():
            if isinstance(value, dict):
                sub_extent, sub_size = write_dir(value)
                rows.append(_record(name, sub_extent, sub_size, True))
            else:
                file_extent = alloc(len(value))
                place(file_extent, value)
                rows.append(_record(name, file_extent, len(value), False))
        place(extent, b''.join(rows))
        return extent, size

    root_extent, root_size = write_dir(tree)
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[156:190] = _record(b'\0', root_extent, root_size, True)
    terminator = bytearray(SECTOR)
    terminator[0] = 255
    terminator[1:6] = b'CD001'
    terminator[6] = 1
    image[16 * SECTOR:17 * SECTOR] = pvd
    image[17 * SECTOR:18 * SECTOR] = terminator
    path.write_bytes(bytes(image))
    return path


class Setup:
    def __init__(self, root, iso, checksum, out, id_file, sha):
        self.root = root
        self.iso = iso
        self.checksum = checksum
        self.out = out
        self.id_file = id_file
        self.sha = sha

    @property
    def state_file(self):
        return self.out / '.kazarov-extract.json'

    def run(self, *extra):
        return subprocess.run(
            [sys.executable, str(KZ_EXTRACT), '--iso', str(self.iso), '--checksum', str(self.checksum),
             '--out', str(self.out), '--id-file', str(self.id_file), *extra],
            capture_output=True, text=True)


def _write_checksum(path, digest, name):
    path.write_text(f'{digest}  {name}\n')
    return path


@pytest.fixture
def setup(tmp_path):
    iso = build_iso(tmp_path / 'Test Disc (PAL).iso', TREE)
    sha = hashlib.sha256(iso.read_bytes()).hexdigest()
    checksum = _write_checksum(tmp_path / 'checksum.sha256', sha, iso.name)
    out = tmp_path / 'extracted'
    return Setup(tmp_path, iso, checksum, out, out / '.kazarov-extract.id', sha)


def _assert_tree(out):
    for rel, data in EXPECTED_FILES.items():
        assert (out / 'iso' / rel).read_bytes() == data, rel


def _age(path, seconds_ago=10_000):
    """Set an mtime far in the past so that a rewrite is visible."""
    old = 1_000_000_000 - seconds_ago
    os.utime(path, (old, old))
    return path.stat().st_mtime_ns


def test_first_run_extracts_the_tree(setup):
    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert 'Hashing' in result.stdout
    assert 'Extracting 5 files' in result.stdout
    _assert_tree(setup.out)
    assert setup.id_file.read_text() == f'{setup.sha} 1\n'
    assert json.loads(setup.state_file.read_text())['sha256'] == setup.sha


def test_second_run_is_a_noop_and_keeps_the_id_mtime(setup):
    assert setup.run().returncode == 0
    id_mtime = _age(setup.id_file)
    file_mtime = _age(setup.out / 'iso' / 'SYSTEM.CNF')

    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert 'Hashing' not in result.stdout
    assert 'Extracting' not in result.stdout
    assert setup.id_file.stat().st_mtime_ns == id_mtime
    assert (setup.out / 'iso' / 'SYSTEM.CNF').stat().st_mtime_ns == file_mtime


def test_touching_the_iso_rehashes_but_keeps_the_id(setup):
    assert setup.run().returncode == 0
    id_mtime = _age(setup.id_file)
    state_before = setup.state_file.read_text()
    os.utime(setup.iso, (2_000_000_000, 2_000_000_000))

    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert 'Hashing' in result.stdout
    assert 'Extracting' not in result.stdout
    assert setup.id_file.stat().st_mtime_ns == id_mtime
    assert setup.state_file.read_text() != state_before
    _assert_tree(setup.out)


def test_deleted_file_is_extracted_again(setup):
    assert setup.run().returncode == 0
    id_mtime = _age(setup.id_file)
    gone = setup.out / 'iso' / 'MOVIE' / 'E54001.PSS'
    gone.unlink()

    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert 'Extracting 1 files' in result.stdout
    assert gone.read_bytes() == b'\xAA' * 3000
    assert setup.id_file.stat().st_mtime_ns == id_mtime


def test_wrong_size_file_is_replaced_and_leftovers_pruned(setup):
    assert setup.run().returncode == 0
    (setup.out / 'iso' / 'SYSTEM.CNF').write_bytes(b'cut')
    stray = setup.out / 'iso' / 'MOVIE' / 'OLD.TXT'
    stray.write_bytes(b'left over')

    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert 'Removed 1 stale file(s)' in result.stdout
    _assert_tree(setup.out)
    assert not stray.exists()


def test_force_extracts_everything_again(setup):
    assert setup.run().returncode == 0
    target = setup.out / 'iso' / 'SCES_511.90'
    target.write_bytes(b'\0' * len(EXPECTED_FILES['SCES_511.90']))

    result = setup.run('--force')
    assert result.returncode == 0, result.stderr
    assert 'Extracting 5 files' in result.stdout
    _assert_tree(setup.out)


def test_new_content_with_same_names_and_sizes_rewrites_every_file(setup):
    assert setup.run().returncode == 0
    changed = dict(TREE)
    changed[b'SCES_511.90;1'] = bytes(b ^ 0xFF for b in TREE[b'SCES_511.90;1'])
    build_iso(setup.iso, changed)
    os.utime(setup.iso, (2_000_000_000, 2_000_000_000))
    new_sha = hashlib.sha256(setup.iso.read_bytes()).hexdigest()
    _write_checksum(setup.checksum, new_sha, setup.iso.name)

    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert 'Extracting 5 files' in result.stdout
    assert (setup.out / 'iso' / 'SCES_511.90').read_bytes() == changed[b'SCES_511.90;1']
    assert setup.id_file.read_text() == f'{new_sha} 1\n'


def test_checksum_mismatch_exits_1_and_writes_nothing(setup):
    _write_checksum(setup.checksum, '0' * 64, setup.iso.name)

    result = setup.run()
    assert result.returncode == 1
    assert 'does not match' in result.stderr
    assert not setup.id_file.exists()
    assert not (setup.out / 'iso').exists()


def test_missing_iso_exits_1(setup):
    setup.iso.unlink()
    result = setup.run()
    assert result.returncode == 1
    assert 'does not exist' in result.stderr


def _iso_tool():
    for tool in ('xorriso', 'genisoimage', 'mkisofs'):
        if shutil.which(tool):
            return tool
    return None


@pytest.mark.skipif(_iso_tool() is None, reason='no xorriso, genisoimage or mkisofs installed')
def test_image_made_by_an_iso_tool(tmp_path):
    source = tmp_path / 'source'
    (source / 'MOVIE').mkdir(parents=True)
    (source / 'SYSTEM.CNF').write_bytes(b'BOOT2 = x\n')
    (source / 'MOVIE' / 'E54001.PSS').write_bytes(b'\x01' * 5000)
    tool = _iso_tool()
    iso = tmp_path / 'tool.iso'
    if tool == 'xorriso':
        cmd = ['xorriso', '-as', 'mkisofs', '-quiet', '-o', str(iso), str(source)]
    else:
        cmd = [tool, '-quiet', '-o', str(iso), str(source)]
    subprocess.run(cmd, check=True)

    sha = hashlib.sha256(iso.read_bytes()).hexdigest()
    checksum = _write_checksum(tmp_path / 'checksum.sha256', sha, iso.name)
    out = tmp_path / 'extracted'
    setup = Setup(tmp_path, iso, checksum, out, out / '.kazarov-extract.id', sha)
    result = setup.run()
    assert result.returncode == 0, result.stderr
    assert (out / 'iso' / 'SYSTEM.CNF').read_bytes() == b'BOOT2 = x\n'
    assert (out / 'iso' / 'MOVIE' / 'E54001.PSS').read_bytes() == b'\x01' * 5000
