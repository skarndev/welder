"""Tests for welder's trust_bindable escape hatches.

welder's bindability gate rejects a program-defined type it cannot see welded.
When the user registers such a type with pybind11 by hand (outside welder's view),
trust_bindable vouches for it so the dependent member still binds — via the member
mark ``[[=welder::mark::trust_bindable]]`` or the type-level customization point
``welder::trust_bindable<T>``. The C++ side lives in tests/pybind11/cpp/trust.hpp.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def trust(mod: ModuleType) -> ModuleType:
    # The cases bind under the `trust` submodule (C++ side: namespace `trust`).
    return cast(ModuleType, mod.trust)


# --- member-mark trust ------------------------------------------------------
def test_member_mark_binds_a_hand_registered_member(trust: ModuleType) -> None:
    obj = trust.TrustsMember()
    # The trusted member is present alongside the ordinary one.
    assert {"item", "count"} <= public_attrs(obj)
    # It round-trips through the hand-registered pybind11 type.
    handmade = trust.Handmade()
    handmade.n = 7
    obj.item = handmade
    assert obj.item.n == 7
    obj.count = 3
    assert obj.count == 3


def test_member_mark_trusts_a_method_signature(trust: ModuleType) -> None:
    # The mark on a method trusts its whole signature (here the return type).
    obj = trust.TrustsMember()
    assert "make" in public_attrs(obj)
    assert obj.make(4).n == 4


# --- type-level trust -------------------------------------------------------
def test_type_level_trust_binds_a_plain_member(trust: ModuleType) -> None:
    obj = trust.TrustsType()
    assert "item" in public_attrs(obj)
    handmade = trust.Handmade2()
    handmade.n = 5
    obj.item = handmade
    assert obj.item.n == 5


def test_type_level_trust_clears_a_container_of_the_trusted_type(trust: ModuleType) -> None:
    # vector<Handmade2> binds because the wrapper table recurses to the trusted leaf.
    obj = trust.TrustsType()
    assert "many" in public_attrs(obj)
    obj.many = [trust.Handmade2(), trust.Handmade2()]
    assert len(obj.many) == 2
