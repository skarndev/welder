"""Method-backed properties (getter/setter marks), observed through the generated
module: a marked accessor pair binds as one idiomatic Python property, a lone
getter as a read-only one, and the accessor functions themselves stop being
methods for the languages their marks cover.

Mirrors tests/common/cpp/properties.hpp (same sections, same order); the Lua
counterpart is tests/lua/spec/properties_spec.lua.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def props(mod: ModuleType) -> ModuleType:
    # The cases bind under the `properties` submodule (C++ namespace `properties`).
    return cast(ModuleType, mod.properties)


# --- overload-style accessors: radius() / radius(double) ---------------------


def test_overload_style_pair_is_a_read_write_property(props: ModuleType) -> None:
    c = props.Circle()
    assert c.radius == 1.0
    c.radius = 2.0
    assert c.radius == 2.0


def test_prefix_style_lone_getter_is_read_only(props: ModuleType) -> None:
    c = props.Circle()
    c.radius = 2.0
    assert c.area == pytest.approx(12.566, abs=1e-3)
    with pytest.raises(AttributeError):
        c.area = 5.0


def test_accessors_are_not_also_methods(props: ModuleType) -> None:
    c = props.Circle()
    # The C++ accessor spellings vanish (they became the properties)...
    assert not callable(c.radius)
    assert not hasattr(c, "get_area")
    # ...while an ordinary method stays a method.
    assert c.diameter() == 2.0


def test_getter_doc_is_the_property_doc(props: ModuleType) -> None:
    assert props.Circle.radius.__doc__ == "The circle's radius."


# --- name derivation across spelling conventions -----------------------------


def test_camel_case_accessors_keep_their_convention(props: ModuleType) -> None:
    v = props.Vehicle()
    assert v.maxSpeed == 60
    v.maxSpeed = 10
    assert v.maxSpeed == 10


def test_mixed_convention_accessors_pair_and_the_getter_spelling_wins(
    props: ModuleType,
) -> None:
    v = props.Vehicle()  # get_scale + SetScale -> one property "scale"
    assert v.scale == 1.0
    v.scale = 2.5
    assert v.scale == 2.5
    assert not hasattr(v, "SetScale")


def test_is_predicate_is_not_stripped(props: ModuleType) -> None:
    v = props.Vehicle()
    assert v.is_ready is True
    with pytest.raises(AttributeError):
        v.is_ready = False  # read-only: no setter participates


# --- explicit names + per-language scoping -----------------------------------


def test_explicit_names_pair_regardless_of_identifiers(props: ModuleType) -> None:
    g = props.Gauge()
    assert g.level == 0.5
    g.level = 0.9
    assert g.level == pytest.approx(0.9)
    assert not hasattr(g, "raw")
    assert not hasattr(g, "assign")


def test_python_scoped_marks_make_a_python_property(props: ModuleType) -> None:
    g = props.Gauge()
    assert g.mode == 1
    g.mode = 3
    assert g.mode == 3
    # The marks cover Python, so the accessors are not methods here (they stay
    # ordinary methods on the Lua side, where the marks don't apply).
    assert not hasattr(g, "get_mode")
    assert not hasattr(g, "set_mode")


def test_language_excluded_setter_leaves_python_read_write(props: ModuleType) -> None:
    g = props.Gauge()  # the setter is excluded for lua only
    g.limit = 5
    assert g.limit == 5


# --- opt_in: an accessor mark implies the opt-in ------------------------------


def test_opt_in_accessor_marks_imply_the_opt_in(props: ModuleType) -> None:
    p = props.Padlock()
    assert p.code == 1234
    p.code = 999
    assert p.code == 999


def test_opt_in_still_drops_unmarked_methods(props: ModuleType) -> None:
    p = props.Padlock()
    assert not hasattr(p, "stray")
    assert p.shown() == 42  # a normal method still needs its include


# --- flattened non-welded base + a fluent (value-returning) setter -----------


def test_flattened_base_accessors_become_properties_on_the_derived(
    props: ModuleType,
) -> None:
    t = props.Tag()
    assert t.label == "blank"
    t.label = "hi"  # the fluent setter's Labeled& return is discarded
    assert t.label == "hi"


# --- protected accessors under weld_protected --------------------------------


def test_protected_accessors_bind_under_weld_protected(props: ModuleType) -> None:
    s = props.Sealed()
    assert s.inner == 7
    s.inner = 12
    assert s.inner == 12