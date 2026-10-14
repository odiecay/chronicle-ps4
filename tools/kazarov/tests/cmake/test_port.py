"""CMake checks for cmake/KazarovPort.cmake, on the consumer fixture in port_fixture/.

Each test configures the fixture into its own build directory. Skipped when cmake, ninja or
glslangValidator is not on PATH, when SDL3 or Vulkan is not installed, or when a pinned
download cannot be reached.
"""

import re
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
FIXTURE = HERE / "port_fixture"
PORT = ROOT / "port"
FAMILIES = ["libvu0", "libpad", "libmc", "sifdev", "sifrpc", "eekernel", "libcdvd", "libgraph", "libdma", "libpkt"]
MISSING_DEPENDENCY = re.compile(
    r'package configuration file provided by "SDL3"|Could NOT find (SDL3|Vulkan)|Could not find (SDL3|Vulkan)')
NETWORK_FAILURE = re.compile(
    r"Could not resolve|Failed to connect|Connection refused|Network is unreachable|Timeout was reached")


@pytest.fixture(autouse=True)
def require_tools():
    for tool in ("cmake", "ninja", "glslangValidator"):
        if shutil.which(tool) is None:
            pytest.skip(tool + " is not on PATH")


def configure(build, *extra):
    return subprocess.run(
        ["cmake", "-G", "Ninja", "-S", str(FIXTURE), "-B", str(build), f"-DKAZAROV_DIR={ROOT}", *extra],
        capture_output=True, text=True)


def configure_or_skip(build):
    result = configure(build)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        if MISSING_DEPENDENCY.search(output):
            pytest.skip("SDL3 or Vulkan is not installed")
        if NETWORK_FAILURE.search(output):
            pytest.skip("a pinned download could not be reached")
    assert result.returncode == 0, output
    return build


def expected_sce_sources():
    sources = []
    for family in FAMILIES:
        for candidate in (PORT / "sce" / f"{family}.cpp", PORT / "sce" / "stubs" / f"{family}.cpp"):
            if candidate.is_file():
                sources.append(candidate.resolve())
    sources.append((PORT / "sce" / "unimplemented.cpp").resolve())
    return sources


def test_sce_sources_for_the_families(tmp_path):
    build = configure_or_skip(tmp_path / "build")
    listed = [Path(line).resolve() for line in (build / "sce_sources.txt").read_text().split()]
    assert listed == expected_sce_sources()


def test_test_sources_include_every_shared_test(tmp_path):
    build = configure_or_skip(tmp_path / "build")
    listed = {Path(line).resolve() for line in (build / "test_sources.txt").read_text().split()}
    tests = sorted((PORT / "tests").glob("*_test.cpp"))
    assert tests
    for test in tests:
        assert test.resolve() in listed


def test_host_library_builds(tmp_path):
    build = configure_or_skip(tmp_path / "build")
    result = subprocess.run(["cmake", "--build", str(build), "--target", "fixture_host"],
                            capture_output=True, text=True, timeout=900)
    assert result.returncode == 0, result.stdout + result.stderr


def test_unknown_sdk_family_fails_configure(tmp_path):
    result = configure(tmp_path / "build", "-DFIXTURE_BAD_FAMILY=ON")
    output = result.stdout + result.stderr
    assert result.returncode != 0
    assert "libnope" in output
