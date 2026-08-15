"""C++ default arguments: the rods bind one truncated overload per omissible
arity, so calling with fewer arguments applies the REAL C++ default."""

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture(scope="session")
def d(mod: ModuleType) -> ModuleType:
    return cast(ModuleType, mod.defaults)


def test_ctor_default_applied(d: ModuleType) -> None:
    assert d.Dial(3).label == "dial"          # name defaulted
    assert d.Dial(3, "mine").label == "mine"  # full arity still works


def test_method_two_trailing_defaults(d: ModuleType) -> None:
    dial = d.Dial()
    assert dial.bump() == 1          # by=1, times=1
    assert dial.bump(5) == 6         # times defaulted
    assert dial.bump(2, 3) == 12     # full arity


def test_method_default_after_required(d: ModuleType) -> None:
    dial = d.Dial(0)
    assert dial.scaled(2) == 20        # factor defaulted to 10
    assert dial.scaled(2, 3) == 6


def test_kwargs_still_work_on_truncated(d: ModuleType) -> None:
    dial = d.Dial()
    assert dial.bump(by=4) == 4


def test_static_method_default(d: ModuleType) -> None:
    assert d.Dial.stride() == 8
    assert d.Dial.stride(10) == 20


def test_free_function_default(d: ModuleType) -> None:
    assert d.spaced(1) == 101
    assert d.spaced(1, 2) == 3
