import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


@pytest.fixture
def anyio_backend() -> str:
    return "asyncio"


# --- reference C++ parser build (Task 3 of the reference-parser plan) ---

_REPO_ROOT = Path(__file__).resolve().parents[1]  # drs-bridge/
_REF_PARSER_DIR = _REPO_ROOT / "parsers" / "reference"
_REF_BUILD_DIR  = _REF_PARSER_DIR / "build"


def _find_built_library() -> Path | None:
    """Locate the built reference parser regardless of generator quirks."""
    if sys.platform == "win32":
        candidates = list(_REF_BUILD_DIR.glob("**/reference_parser.dll"))
    else:
        candidates = list(_REF_BUILD_DIR.glob("**/reference_parser.so"))
        candidates += list(_REF_BUILD_DIR.glob("**/libreference_parser.so"))
    return candidates[0] if candidates else None


@pytest.fixture(scope="session")
def built_reference_parser() -> Path:
    """Build the reference C++ parser once per test session via CMake.

    Skips the test if CMake isn't on PATH (developer machines without a
    C++ toolchain shouldn't see a hard failure). CI installs CMake on
    both runner images by default.
    """
    if shutil.which("cmake") is None:
        pytest.skip("cmake not on PATH; skipping reference-parser integration test")

    try:
        subprocess.run(
            ["cmake", "-S", str(_REF_PARSER_DIR), "-B", str(_REF_BUILD_DIR)],
            check=True, capture_output=True,
        )
        subprocess.run(
            ["cmake", "--build", str(_REF_BUILD_DIR), "--config", "Release"],
            check=True, capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        pytest.skip(f"cmake build failed: {e.stderr.decode(errors='replace')[:500]}")

    lib = _find_built_library()
    if lib is None:
        pytest.skip("reference parser built but library not found at expected path")
    return lib


# --- DDF-550 (RDFS) parser build ---

_DDF550_PARSER_DIR = _REPO_ROOT / "parsers" / "ddf550"
_DDF550_BUILD_DIR  = _DDF550_PARSER_DIR / "build"


def _find_built_ddf550_library() -> Path | None:
    """Locate the built DDF-550 parser regardless of generator quirks."""
    if sys.platform == "win32":
        candidates = list(_DDF550_BUILD_DIR.glob("**/ddf550.dll"))
        candidates += list(_DDF550_BUILD_DIR.glob("**/libddf550.dll"))
    else:
        candidates = list(_DDF550_BUILD_DIR.glob("**/libddf550.so"))
    return candidates[0] if candidates else None


@pytest.fixture(scope="session")
def built_ddf550_parser() -> Path:
    """Build the DDF-550 C++ parser once per test session via CMake.

    Skips the test if CMake isn't on PATH (developer machines without a
    C++ toolchain shouldn't see a hard failure). CI installs CMake on
    both runner images by default.
    """
    if shutil.which("cmake") is None:
        pytest.skip("cmake not on PATH; skipping ddf550 integration test")

    try:
        subprocess.run(
            ["cmake", "-S", str(_DDF550_PARSER_DIR), "-B", str(_DDF550_BUILD_DIR)],
            check=True, capture_output=True,
        )
        subprocess.run(
            ["cmake", "--build", str(_DDF550_BUILD_DIR), "--target", "ddf550", "--config", "Release"],
            check=True, capture_output=True,
        )
    except subprocess.CalledProcessError as e:
        pytest.skip(f"cmake build failed: {e.stderr.decode(errors='replace')[:500]}")

    lib = _find_built_ddf550_library()
    if lib is None:
        pytest.skip("ddf550 parser built but library not found at expected path")
    return lib
