"""Ninja graph checks for cmake/Kazarov.cmake, on the fixture in fixture/.

Each test configures the fixture into its own build directory, with the fake compiler and
assembler, and asks ninja what it would do. Skipped while kz-mwcc, kz-mwcc-exec or
kz-split-stamp is not in bin/.
"""

import os
import re
import shutil
import subprocess
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURE = HERE / "fixture"
BIN = ROOT / "bin"
NEEDED_TOOLS = ("kz-mwcc", "kz-mwcc-exec", "kz-split-stamp")
DESCRIPTION = re.compile(r"^\[\d+/\d+\] (.*)$", re.M)


@pytest.fixture(autouse=True)
def require_tools():
    missing = [name for name in NEEDED_TOOLS if not (BIN / name).is_file()]
    if missing:
        pytest.skip("not present yet in bin/: " + ", ".join(missing))
    for tool in ("cmake", "ninja"):
        if shutil.which(tool) is None:
            pytest.skip(tool + " is not on PATH")


def run(args, env=None):
    result = subprocess.run(args, capture_output=True, text=True, env=env)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def configure(tmp_path, *extra):
    build = tmp_path / "build"
    run(["cmake", "-S", str(FIXTURE), "-B", str(build), "-G", "Ninja", *extra])
    return build


def build_all(build, env=None):
    run(["ninja", "-C", str(build)], env=env)


def pending(build):
    """What ninja would run now, one description per edge."""
    out = run(["ninja", "-C", str(build), "-n"])
    if "no work to do" in out:
        return []
    return DESCRIPTION.findall(out)


def touch(path):
    os.utime(path, None)


def test_second_build_is_a_no_op(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    assert "no work to do" in run(["ninja", "-C", str(build), "-n"])


def test_private_header_rebuilds_exactly_one_object(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    touch(FIXTURE / "src" / "a_private.h")
    assert pending(build) == ["CC src/a.c"]


def test_shared_header_rebuilds_both_compiled_objects(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    touch(FIXTURE / "src" / "shared.h")
    assert sorted(pending(build)) == ["CC src/a.c", "CC src/b.c"]


def test_raw_object_rebuilds_only_for_its_source(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    touch(FIXTURE / "src" / "c.c")
    assert pending(build) == ["CC src/c.c"]


def test_touching_a_post_tool_rebuilds_both_objects(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    touch(FIXTURE / "tools" / "post.py")
    assert sorted(pending(build)) == ["CC src/a.c", "CC src/b.c"]


def test_split_restore_leaves_objects_up_to_date(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    # Forces the split: it rewrites unit_c.s with the same content, and the restore step
    # must put the old mtimes back so the assembled object is not stale.
    run(["ninja", "-C", str(build), "disassemble"])
    assert pending(build) == []


def test_split_content_change_reassembles_only_that_unit(tmp_path):
    build = configure(tmp_path)
    build_all(build)
    env = dict(os.environ, FIXTURE_VARIANT="2")
    run(["ninja", "-C", str(build), "disassemble"], env=env)
    descriptions = pending(build)
    assert len(descriptions) == 1
    assert descriptions[0].startswith("AS ") and descriptions[0].endswith("unit_c.s")


def test_compiler_is_an_input_of_each_compile(tmp_path):
    build = configure(tmp_path)
    edges = [line for line in (build / "build.ninja").read_text().splitlines()
             if line.startswith("build ") and "obj/a.o: CUSTOM_COMMAND" in line]
    assert len(edges) == 1
    assert str(HERE / "fake_cc.py") in edges[0]


def test_object_commands_carry_the_image_id(tmp_path):
    build = configure(tmp_path)
    commands = [line for line in (build / "build.ninja").read_text().splitlines()
                if line.startswith("  COMMAND = ") and ("kz-mwcc " in line or "fake_as.py" in line)]
    assert len(commands) == 4
    assert all("KZ_IMAGE_ID=" in line for line in commands)


def test_iso_directory_reconfigures_when_the_disc_is_added(tmp_path):
    # rom/ holds no disc in the fixture; its directory must still be a configure dependency.
    build = configure(tmp_path)
    rerun = [line for line in (build / "build.ninja").read_text().splitlines()
             if line.startswith("build build.ninja: RERUN_CMAKE")]
    assert len(rerun) == 1
    assert str(FIXTURE / "rom") in rerun[0]


def test_missing_iso_directory_watches_its_nearest_ancestor(tmp_path):
    ancestor = tmp_path / "discs"
    ancestor.mkdir()
    build = configure(tmp_path, "-DFIXTURE_ISO=%s" % (ancestor / "new" / "game.iso"))
    rerun = [line for line in (build / "build.ninja").read_text().splitlines()
             if line.startswith("build build.ninja: RERUN_CMAKE")]
    assert len(rerun) == 1
    assert str(ancestor) in rerun[0]
    assert str(ancestor / "new") not in rerun[0]


def test_disassemble_runs_after_setup(tmp_path):
    build = configure(tmp_path)
    text = (build / "build.ninja").read_text()
    edge = [line for line in text.splitlines() if line.startswith("build disassemble: phony")]
    assert len(edge) == 1
    assert "setup" in edge[0]


def test_assembler_is_an_input_of_each_assembly(tmp_path):
    build = configure(tmp_path)
    edges = [line for line in (build / "build.ninja").read_text().splitlines()
             if line.startswith("build ") and "unit_c.o: CUSTOM_COMMAND" in line]
    assert len(edges) == 1
    assert str(FIXTURE / "tools" / "fake_as.py") in edges[0]


def test_touching_the_assembler_script_rebuilds_assembly(tmp_path):
    # KZ_AS is "python3 tools/fake_as.py" in the fixture: the script is an input, not only python3.
    build = configure(tmp_path)
    build_all(build)
    touch(FIXTURE / "tools" / "fake_as.py")
    pending_now = pending(build)
    assert len(pending_now) == 1
    assert pending_now[0].startswith("AS ")
    assert pending_now[0].endswith("unit_c.s")


def test_single_word_assembler_configures(tmp_path):
    # The projects name the assembler by one word; only the fixture uses a command list.
    result = subprocess.run(
        ["cmake", "-DKZ_AS=as", f"-DCMAKE_SOURCE_DIR={tmp_path}", f"-DKZ_ROOT={ROOT}",
         "-P", str(ROOT / "cmake" / "KazarovToolchain.cmake")],
        capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
