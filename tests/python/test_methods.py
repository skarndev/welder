"""Behavioral tests for bound constructors and methods (the resolution side —
which members are exposed — lives in test_resolution.py).
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def meth(mod: ModuleType) -> ModuleType:
    # The cases bind under the `methods` submodule (C++ side: namespace `methods`).
    return cast(ModuleType, mod.methods)


@pytest.mark.parametrize(
    ("args", "expected_value"),
    [
        pytest.param((), 0, id="default-constructor"),
        pytest.param((5,), 5, id="int-constructor"),
    ],
)
def test_constructor(meth: ModuleType, args: tuple[int, ...], expected_value: int) -> None:
    assert meth.Counter(*args).value() == expected_value


def test_method_mutation(meth: ModuleType) -> None:
    c = meth.Counter(0)
    c.increment()
    c.increment()
    c.add(10)
    assert c.value() == 12


def test_static_method(meth: ModuleType) -> None:
    assert meth.Counter.version() == 7


@pytest.mark.parametrize(
    ("args", "expected"),
    [
        pytest.param((5,), 15, id="one-arg-overload"),
        pytest.param((5, 5), 20, id="two-arg-overload"),
    ],
)
def test_overloaded_method_dispatch(
    meth: ModuleType, args: tuple[int, ...], expected: int
) -> None:
    assert meth.Calc(10).sum(*args) == expected


# --- argument names ---------------------------------------------------------
def test_method_argument_is_named(meth: ModuleType) -> None:
    # The C++ parameter name reaches Python (a keyword arg), not arg0.
    assert meth.Counter(0).add(n=3) is None
    assert "n: int" in meth.Counter.add.__doc__


def test_constructor_argument_is_named(meth: ModuleType) -> None:
    assert meth.Counter(start=9).value() == 9


def test_free_function_arguments_are_named(mod: ModuleType) -> None:
    # A free function borrowed from the `documented` submodule: its C++ parameter
    # names reach Python as keyword arguments.
    assert mod.documented.add(a=2, b=3) == 5


# --- aggregate initialization -----------------------------------------------
def test_aggregate_field_constructor(meth: ModuleType) -> None:
    v = meth.Vec2(1.5, 2.5)
    assert (v.x, v.y) == (1.5, 2.5)


def test_aggregate_keyword_constructor(meth: ModuleType) -> None:
    v = meth.Vec2(x=1.0, y=2.0)
    assert (v.x, v.y) == (1.0, 2.0)


def test_aggregate_default_constructor_still_bound(meth: ModuleType) -> None:
    v = meth.Vec2()
    assert (v.x, v.y) == (0.0, 0.0)
