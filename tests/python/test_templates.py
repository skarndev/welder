"""Tests for class-template instantiations welded through namespace-scope aliases.

``members_of(ns)`` enumerates the class *template*, never an instantiation, so a
namespace-scope ``using IntCrate = Crate<int>;`` is the way an instantiation enters
a ``weld_namespace`` sweep: the alias supplies both the C++ spelling and the default
target-language name — no stringified name anywhere. Mirrors
``tests/common/cpp/templates.hpp``; the same cases run as a busted spec on the Lua
rods (cross-rod consistency).
"""

# Behavioral runtime tests reach the extension through the ModuleType fixture (an
# ``Any``), as in the other runtime specs.
# mypy: disable-error-code="misc, name-defined"

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def tpl(mod: ModuleType) -> ModuleType:
    sub = getattr(mod, "templates", None)
    if sub is None:
        pytest.skip("backend has no `templates` submodule (alias-welded instantiations) yet")
    return cast(ModuleType, sub)


def test_alias_welds_an_instantiation(tpl: ModuleType) -> None:
    c = tpl.IntCrate()
    assert c.get() == 0
    c.put(7)
    assert c.get() == 7
    assert c.item == 7  # the field binds like any welded type's

    # The bare template never binds — there is nothing to bind until instantiated.
    assert not hasattr(tpl, "Crate")


def test_two_instantiations_of_one_template(tpl: ModuleType) -> None:
    w = tpl.WordCrate()
    w.put("hello")
    assert w.get() == "hello"

    # Distinct Python types, independent state.
    assert tpl.WordCrate is not tpl.IntCrate
    assert tpl.IntCrate().get() == 0


def test_weld_as_on_the_alias_renames_verbatim(tpl: ModuleType) -> None:
    assert hasattr(tpl, "CrateOfDouble")
    assert not hasattr(tpl, "RenamedCrate")  # the identifier is not used
    d = tpl.CrateOfDouble()
    d.put(2.5)
    assert d.get() == 2.5


def test_weld_as_on_the_template_applies_through_the_alias(tpl: ModuleType) -> None:
    # Tagged<T> carries weld_as("TaggedBox"); the alias TaggedInt has none of its
    # own, so the template's (read through the instantiation) wins over the
    # identifier.
    assert hasattr(tpl, "TaggedBox")
    assert not hasattr(tpl, "TaggedInt")
    assert tpl.TaggedBox().tag == 0


def test_alias_weld_opts_in_an_unwelded_template(tpl: ModuleType) -> None:
    # vendor_tpl::Pack carries NO weld (third-party-style); the weld on the
    # alias-declaration itself opts this one instantiation in.
    p = tpl.IntPack()
    assert p.unwrap() == 0
    p.payload = 9
    assert p.unwrap() == 9