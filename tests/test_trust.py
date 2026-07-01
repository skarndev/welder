"""Tests for welder's trust_bindable escape hatches.

welder's bindability gate rejects a program-defined type it cannot see welded.
When the user registers such a type with pybind11 by hand (outside welder's view),
trust_bindable vouches for it so the dependent member still binds — via the member
mark ``[[=welder::mark::trust_bindable]]`` or the type-level customization point
``welder::trust_bindable<T>``. The C++ side lives in tests/pybind11/cpp/trust.hpp.
"""

from __future__ import annotations

from types import ModuleType

from conftest import public_attrs


# --- member-mark trust ------------------------------------------------------
def test_member_mark_binds_a_hand_registered_member(mod: ModuleType) -> None:
    obj = mod.TrustsMember()
    # The trusted member is present alongside the ordinary one.
    assert {"item", "count"} <= public_attrs(obj)
    # It round-trips through the hand-registered pybind11 type.
    handmade = mod.Handmade()
    handmade.n = 7
    obj.item = handmade
    assert obj.item.n == 7
    obj.count = 3
    assert obj.count == 3


def test_member_mark_trusts_a_method_signature(mod: ModuleType) -> None:
    # The mark on a method trusts its whole signature (here the return type).
    obj = mod.TrustsMember()
    assert "make" in public_attrs(obj)
    assert obj.make(4).n == 4


# --- type-level trust -------------------------------------------------------
def test_type_level_trust_binds_a_plain_member(mod: ModuleType) -> None:
    obj = mod.TrustsType()
    assert "item" in public_attrs(obj)
    handmade = mod.Handmade2()
    handmade.n = 5
    obj.item = handmade
    assert obj.item.n == 5


def test_type_level_trust_clears_a_container_of_the_trusted_type(mod: ModuleType) -> None:
    # vector<Handmade2> binds because the wrapper table recurses to the trusted leaf.
    obj = mod.TrustsType()
    assert "many" in public_attrs(obj)
    obj.many = [mod.Handmade2(), mod.Handmade2()]
    assert len(obj.many) == 2
