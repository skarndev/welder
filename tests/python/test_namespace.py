"""Tests for welder::pybind11::bind_namespace — exposing a whole namespace at once.

The C++ namespace ``catalog`` is bound under the ``catalog`` submodule of the test
module. welder walks it and exposes every member carrying a ``weld``: classes,
free functions, namespace-scope variables, and nested namespaces (as submodules).
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def cat(mod: ModuleType) -> ModuleType:
    # mod.<attr> is Any (ModuleType.__getattr__); cast back at this boundary so
    # the dynamic access stays contained and strict mypy still covers the rest.
    return cast(ModuleType, mod.catalog)


@pytest.fixture()
def manual(mod: ModuleType) -> ModuleType:
    # The `manual` submodule is filled the semi-manual way — weld_function /
    # weld_variable on hand-picked entities — instead of welding a whole namespace.
    return cast(ModuleType, mod.manual)


@pytest.fixture()
def foreign(mod: ModuleType) -> ModuleType:
    # The `foreign` submodule is an *unmarked* C++ namespace bound greedily by the
    # tack-welding carriage (no weld() annotations anywhere in it).
    return cast(ModuleType, mod.foreign)


# --- classes ----------------------------------------------------------------
def test_welded_class_is_exposed(cat: ModuleType) -> None:
    assert cat.Item(7).get_id() == 7


def test_non_welded_class_is_skipped(cat: ModuleType) -> None:
    assert not hasattr(cat, "Hidden")


def test_inheritance_within_a_bound_namespace(cat: ModuleType) -> None:
    # The welded base is registered before its derived (declaration order), so
    # native inheritance still resolves through namespace binding.
    assert issubclass(cat.Cat, cat.Animal2)
    c = cat.Cat()
    assert c.legs == 4 and c.whiskers == 12


# --- free functions ---------------------------------------------------------
@pytest.mark.parametrize(
    ("args", "expected"),
    [
        pytest.param((2, 3), 5, id="two-arg-overload"),
        pytest.param((9,), 9, id="one-arg-overload"),
    ],
)
def test_welded_function_and_overloads(cat: ModuleType, args: tuple[int, ...], expected: int) -> None:
    assert cat.total(*args) == expected


def test_non_welded_function_is_skipped(cat: ModuleType) -> None:
    assert not hasattr(cat, "internal_helper")


def test_welded_but_excluded_function_is_suppressed(cat: ModuleType) -> None:
    # `weld` makes it a candidate; mark::exclude(py) resolves it out.
    assert not hasattr(cat, "suppressed")


# --- variables become module attributes -------------------------------------
@pytest.mark.parametrize(
    ("name", "value"),
    [pytest.param("LIMIT", 100, id="int"), pytest.param("TAG", "catalog", id="string")],
)
def test_welded_variable_becomes_attribute(cat: ModuleType, name: str, value: object) -> None:
    assert getattr(cat, name) == value


def test_non_welded_variable_is_skipped(cat: ModuleType) -> None:
    assert not hasattr(cat, "PRIVATE_LIMIT")


# --- mutable variables become live properties -------------------------------
def test_mutable_variable_reads_live_from_cpp(cat: ModuleType) -> None:
    start = cat.counter
    cat.bump()
    cat.bump()
    assert cat.counter == start + 2  # reflects the C++ global, not a snapshot


def test_mutable_variable_writes_through_to_cpp(cat: ModuleType) -> None:
    setattr(cat, "counter", 500)  # noqa: B010 — module attr set; ModuleType has no static slot
    cat.bump()  # C++ increments the same global
    assert cat.counter == 501


# --- nested namespaces ------------------------------------------------------
def test_nested_namespace_becomes_a_submodule(cat: ModuleType) -> None:
    assert cat.sub.Nested().v == 5


def test_namespace_without_welded_content_is_skipped(cat: ModuleType) -> None:
    assert not hasattr(cat, "quiet")


def test_opt_in_namespace_binds_only_included_members(cat: ModuleType) -> None:
    assert cat.strict.chosen() == 2          # welded + included
    assert not hasattr(cat.strict, "candidate")  # welded, not included -> skipped


def test_opt_in_parent_recurses_nested_namespace_only_if_included(cat: ModuleType) -> None:
    assert cat.strict.shown.Gizmo().g == 9   # nested namespace included -> recursed
    assert not hasattr(cat.strict, "omitted")  # not included under opt_in -> skipped


def test_excluded_namespace_is_pruned(cat: ModuleType) -> None:
    # Under the (automatic) parent, a namespace recurses unless excluded.
    assert not hasattr(cat, "secret")


# --- semi-manual binding (weld_function / weld_variable) ---------------------
def test_weld_function_binds_a_single_free_function(manual: ModuleType) -> None:
    assert manual.scale(6, 7) == 42


def test_weld_variable_binds_a_constant_snapshot(manual: ModuleType) -> None:
    assert manual.MANUAL_CONST == 42


def test_weld_variable_binds_a_live_mutable_global(manual: ModuleType) -> None:
    start = manual.manual_counter
    manual.manual_bump()  # a directly welded free function mutating the C++ global
    manual.manual_bump()
    assert manual.manual_counter == start + 2  # live property, not a snapshot


def test_call_site_name_override_wins(manual: ModuleType) -> None:
    # A verbatim `name` argument to weld_function / weld_variable is used as-is,
    # taking precedence over the entity's own identifier/styled/weld_as name.
    assert manual.renamed_fn(5) == 6
    assert manual.RENAMED_CONST == 99
    assert not hasattr(manual, "renamable")  # bound only under the override name
    assert not hasattr(manual, "RENAMABLE")


# --- tack welding: bind an unmarked namespace greedily ----------------------
def test_tack_binds_unmarked_class(foreign: ModuleType) -> None:
    w = foreign.Widget()
    assert w.size == 3
    assert w.doubled() == 6  # a method, bound with no weld() on the class


def test_tack_binds_unmarked_nested_type(foreign: ModuleType) -> None:
    # A NESTED type in the unmarked library: the greedy pass sweeps member types
    # like the stitch one, so Widget.Stat registers under Widget and stats()'
    # signature passes the gate without a trust hatch.
    assert foreign.Widget.Stat().uses == 0
    assert foreign.Widget().stats().uses == 3


def test_tack_never_sweeps_member_aliases(foreign: ModuleType) -> None:
    # Under greedy resolution every complete type passes the gate, so a member
    # alias has nothing left to register — Widget.Twin must not appear.
    assert not hasattr(foreign.Widget, "Twin")


def test_tack_binds_unmarked_free_function(foreign: ModuleType) -> None:
    assert foreign.add(2, 5) == 7


def test_tack_binds_unmarked_constant(foreign: ModuleType) -> None:
    assert foreign.VERSION == 7


def test_tack_recurses_unmarked_nested_namespace(foreign: ModuleType) -> None:
    assert foreign.nested.Gadget().id == 5


def test_tack_accepts_own_types_in_signatures(foreign: ModuleType) -> None:
    # The library's own (unmarked) class types appear in signatures; the greedy
    # registration oracle accepts them without a trust_bindable hatch, and the
    # bindings work because the same tack pass registers the types.
    a, b = foreign.Widget(), foreign.Widget()
    assert a.merged(b).size == 6  # method: Widget param + Widget return
    c = foreign.fuse(a, b)  # free function returning the FORWARD-declared Coupler
    assert (c.left.size, c.right.size) == (3, 3)
    assert foreign.gadget_id(foreign.nested.Gadget()) == 5  # nested-namespace type


def test_tack_weld_protected_knob(mod: ModuleType) -> None:
    # foreign_protected is tacked with greedy_resolution<true> — the blanket
    # protected opt-in for a library that cannot carry policy::weld_protected.
    # (The plain tack of `foreign` keeps the public-only default.)
    p = mod.foreign_protected.Panel()
    assert p.trim() == 10  # protected method, bound by the knob
    assert p.width == 4  # protected data, bound read/write
    p.width = 6
    assert p.frame() == 16
    assert not hasattr(p, "serial")  # private: out under every knob/resolution


def test_resolution_hook_receives_bound_into(mod: ModuleType) -> None:
    # foreign_mixed is tacked with a resolution whose protected_participates
    # hook admits only when bound_into == Display. Meter DECLARES the protected
    # reading(); Display merely inherits it (Meter is unmarked, so it flattens).
    # The member appearing on Display but not on Meter proves the carriage
    # hands the hook the WELDED type (the flattening target), not the member's
    # declaring class.
    fm = mod.foreign_mixed
    d = fm.Display()
    assert d.reading() == 55  # flattened protected member, admitted into Display
    assert d.model() == 1  # public members flatten as usual
    m = fm.Meter()
    assert m.model() == 1
    assert not hasattr(m, "reading")  # same member, refused on Meter's own binding
