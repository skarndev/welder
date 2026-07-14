"""Per-overload / per-constructor marks (mirrors tests/common/cpp/overloads.hpp).

Every overload of a name — and every constructor — resolves independently through
the marks; the carriage hands each name's participating overload group to the rod
whole, so the bound set is identical on every rod. This suite asserts the Python
face; tests/lua/spec/overloads_spec.lua asserts the (differently-marked) Lua one.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture(scope="session")
def ov(mod: ModuleType) -> ModuleType:
    return cast(ModuleType, mod.overloads)


# --- method overloads --------------------------------------------------------
def test_surviving_method_overloads_dispatch(ov: ModuleType) -> None:
    c = ov.Calc(5)
    assert c.apply(1) == 7  # value + x + 1
    assert c.apply("x", 2) == "x:7"  # kept for py (excluded for lua only)


def test_excluded_method_overload_does_not_bind(ov: ModuleType) -> None:
    c = ov.Calc(5)
    with pytest.raises(TypeError):
        c.apply("a", "b")  # the (string, string) overload is excluded everywhere


# --- constructor overloads ---------------------------------------------------
def test_surviving_constructors_bind(ov: ModuleType) -> None:
    assert ov.Calc().value == 0
    assert ov.Calc(9).value == 9


def test_excluded_constructor_does_not_bind(ov: ModuleType) -> None:
    with pytest.raises(TypeError):
        ov.Calc("a", "b")  # the (string, string) constructor is excluded


# --- free-function overloads -------------------------------------------------
def test_free_function_overloads_resolve_per_overload(ov: ModuleType) -> None:
    assert ov.pick(4) == 4
    assert ov.pick("s") == "s!"  # kept for py (excluded for lua only)


# --- opt_in policy vs constructibility ----------------------------------------
def test_opt_in_governs_members_not_constructors(ov: ModuleType) -> None:
    # The unmarked constructor stays usable under policy::opt_in...
    o = ov.OptInCtor(7)
    assert o.kept == 7  # ...and only the included member is exposed.
    assert not hasattr(o, "hidden")