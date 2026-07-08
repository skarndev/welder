"""Shared fixtures for the welder binding tests.

These specs assert welder's backend-agnostic semantics, so they are shared by
every backend (tests/pybind11, tests/nanobind, ...). The compiled extension under
test is selected by the ``WELDER_TEST_MODULE`` environment variable (set per
backend by CTest), so the same suite runs against each backend's build. Its
directory must be importable (CTest puts it on ``PYTHONPATH``).
"""

from __future__ import annotations

import importlib
import os
from types import ModuleType

import pytest


@pytest.fixture(scope="session")
def mod() -> ModuleType:
    name = os.environ.get("WELDER_TEST_MODULE", "welder_test_pybind11")
    return importlib.import_module(name)


def public_attrs(obj: object) -> set[str]:
    """Names bound on an instance, excluding pybind/dunder internals."""
    return {a for a in dir(obj) if not a.startswith("_")}
