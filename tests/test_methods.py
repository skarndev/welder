"""Behavioral tests for bound constructors and methods (the resolution side —
which members are exposed — lives in test_resolution.py).
"""

from __future__ import annotations

from types import ModuleType

import pytest


@pytest.mark.parametrize(
    ("args", "expected_value"),
    [
        pytest.param((), 0, id="default-constructor"),
        pytest.param((5,), 5, id="int-constructor"),
    ],
)
def test_constructor(mod: ModuleType, args: tuple[int, ...], expected_value: int) -> None:
    assert mod.Counter(*args).value() == expected_value


def test_method_mutation(mod: ModuleType) -> None:
    c = mod.Counter(0)
    c.increment()
    c.increment()
    c.add(10)
    assert c.value() == 12


def test_static_method(mod: ModuleType) -> None:
    assert mod.Counter.version() == 7


@pytest.mark.parametrize(
    ("args", "expected"),
    [
        pytest.param((5,), 15, id="one-arg-overload"),
        pytest.param((5, 5), 20, id="two-arg-overload"),
    ],
)
def test_overloaded_method_dispatch(
    mod: ModuleType, args: tuple[int, ...], expected: int
) -> None:
    assert mod.Calc(10).sum(*args) == expected
