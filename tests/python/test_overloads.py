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


# --- opt_in resolves constructors symmetrically --------------------------------
def test_opt_in_filters_unmarked_constructors(ov: ModuleType) -> None:
    # The default ctor is exempt (an implicit one has nothing to mark)...
    o = ov.OptInCtor()
    assert o.kept == 0
    # ...the unmarked value ctor is filtered, the included one binds.
    with pytest.raises(TypeError):
        ov.OptInCtor(7)
    assert ov.OptInCtor(3, 4).kept == 7
    assert not hasattr(ov.OptInCtor(3, 4), "hidden")


# --- explicit constructor emptiness --------------------------------------------
def test_factory_only_surface(ov: ModuleType) -> None:
    # Every ctor mark::exclude-d: not constructible from Python (the explicit
    # escape from the no-constructor-left guard), instances arrive from C++.
    with pytest.raises(TypeError):
        ov.FactoryOnly(1)
    assert ov.forge(9).id == 9


def test_declared_default_ctor_honors_exclude(ov: ModuleType) -> None:
    with pytest.raises(TypeError):
        ov.NoDefault()
    assert ov.NoDefault(5).v == 5