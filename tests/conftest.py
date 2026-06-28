"""Shared fixtures for the welder binding tests.

The compiled extension under test is selected by the ``WELDER_TEST_MODULE``
environment variable (set per-variant by CTest), so the same test suite runs
against both the module-form and header-only-form builds. Its directory must be
importable (CTest puts it on ``PYTHONPATH``).
"""

from __future__ import annotations

import importlib
import os
from types import ModuleType

import pytest


@pytest.fixture(scope="session")
def mod() -> ModuleType:
    name = os.environ.get("WELDER_TEST_MODULE", "welder_test_bindings")
    return importlib.import_module(name)


def public_attrs(obj: object) -> set[str]:
    """Names bound on an instance, excluding pybind/dunder internals."""
    return {a for a in dir(obj) if not a.startswith("_")}
