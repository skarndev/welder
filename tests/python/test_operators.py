"""Tests for binding overloaded operators as Python special methods.

A member operator on a welded type is exposed under its matching dunder
(operator+ -> __add__, operator== -> __eq__, ...). See tests/pybind11/cpp/operators.hpp.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def ops(mod: ModuleType) -> ModuleType:
    # The cases bind under the `operators` submodule (C++ side: namespace `operators`).
    return cast(ModuleType, mod.operators)


def test_binary_add(ops: ModuleType) -> None:
    r = ops.Vec(1.0, 2.0) + ops.Vec(3.0, 4.0)
    assert (r.x, r.y) == (4.0, 6.0)


def test_binary_sub(ops: ModuleType) -> None:
    r = ops.Vec(5.0, 7.0) - ops.Vec(1.0, 2.0)
    assert (r.x, r.y) == (4.0, 5.0)


def test_unary_neg(ops: ModuleType) -> None:
    # operator-() (0 params) binds to __neg__, distinct from binary operator-.
    r = -ops.Vec(1.0, -2.0)
    assert (r.x, r.y) == (-1.0, 2.0)


def test_scalar_mul(ops: ModuleType) -> None:
    r = ops.Vec(1.5, 2.0) * 2.0
    assert (r.x, r.y) == (3.0, 4.0)


def test_equality(ops: ModuleType) -> None:
    assert ops.Vec(1.0, 2.0) == ops.Vec(1.0, 2.0)
    assert ops.Vec(1.0, 2.0) != ops.Vec(3.0, 4.0)


def test_subscript(ops: ModuleType) -> None:
    v = ops.Vec(1.0, 2.0)
    assert (v[0], v[1]) == (1.0, 2.0)


def test_assignment_operator_is_not_bound(ops: ModuleType) -> None:
    # operator= is a special member; it must not surface as a Python method.
    assert not hasattr(ops.Vec, "__assign__")


# --- Heterogeneous operators: the right-hand operand is a different type. ---


def test_operator_over_welded_operand(ops: ModuleType) -> None:
    # Meters.operator+(Feet): both types are welded, so the dunder converts the
    # Feet argument and the mixed-type addition works end to end.
    r = ops.Meters(1.0) + ops.Feet(1.0)
    assert isinstance(r, ops.Meters)
    assert r.value == 1.0 + 0.3048


def test_operator_over_welded_operand_rejects_wrong_type(ops: ModuleType) -> None:
    # The dunder is typed to Feet; a bare float is not a Feet and is rejected
    # (no implicit conversion registered).
    with pytest.raises(TypeError):
        ops.Meters(1.0) + 2.0


def test_operator_over_unwelded_operand_is_rejected(ops: ModuleType) -> None:
    # A member operator whose *other* operand is not welded (e.g.
    # `Tagged operator+(const RawTag&)` with RawTag unwelded) cannot be bound:
    # pybind11 has no converter for the operand, so the dunder would be dead AND
    # its stub would reference an unimportable type. welder's bindability gate
    # makes that a hard *compile* error, so no such type can reach this module —
    # hence there is nothing to call here. The compile-time behavior is pinned by
    # the negcompile.operand_not_welded CTest (tests/pybind11/cpp/neg/); this
    # runtime assert just guards against the pair being reintroduced as a binding.
    assert not hasattr(ops, "Tagged")


def test_free_operator_is_bound(ops: ModuleType) -> None:
    # Coin's operator+ is a free (non-member) function defined separately from
    # the class. welder sweeps the type's enclosing namespace for operators
    # anchored on it, so the free operator binds as __add__ like a member one.
    assert (ops.Coin(1) + ops.Coin(2)).cents == 3


def test_free_operator_honors_marks(ops: ModuleType) -> None:
    # The mark::exclude'd free operator- resolves out exactly like an excluded
    # member operator: no __sub__.
    assert not hasattr(ops.Coin, "__sub__")
    with pytest.raises(TypeError):
        ops.Coin(3) - ops.Coin(1)


def test_member_and_free_operator_share_a_slot(ops: ModuleType) -> None:
    # Mixed has a member operator+(Mixed) AND a free operator+(Mixed, int): the
    # carriage hands both to the rod as one __add__ group, so both call forms work.
    assert (ops.Mixed(3) + ops.Mixed(4)).v == 7
    assert (ops.Mixed(3) + 4).v == 7


def test_reflected_free_operator(ops: ModuleType) -> None:
    # operator*(double, Scaled) has the welded type on the RIGHT: it binds as
    # __rmul__, so both 2 * s and s * 2 work (the latter via operator*(Scaled,
    # double) under plain __mul__).
    assert (ops.Scaled(3.0) * 2.0).f == 6.0
    assert (2.0 * ops.Scaled(3.0)).f == 6.0


def test_free_ostream_inserter_is_str(ops: ModuleType) -> None:
    # operator<<(std::ostream&, Scaled) becomes __str__.
    assert str(ops.Scaled(2.5)) == "Scaled(2.5)"


# --- comparisons synthesized from operator<=> --------------------------------


def test_defaulted_spaceship_full_comparison_set(ops: ModuleType) -> None:
    # A defaulted <=> synthesizes __lt__/__le__/__gt__/__ge__, and its implicitly
    # declared defaulted operator== binds as __eq__ — the full comparison set.
    assert ops.Version(1, 2) < ops.Version(1, 3)
    assert ops.Version(1, 2) <= ops.Version(1, 2)
    assert ops.Version(2, 0) > ops.Version(1, 9)
    assert ops.Version(2, 0) >= ops.Version(2, 0)
    assert ops.Version(1, 2) == ops.Version(1, 2)
    assert ops.Version(1, 2) != ops.Version(1, 3)


def test_custom_spaceship_synthesizes_relationals_only(ops: ModuleType) -> None:
    # A custom (non-defaulted) <=> synthesizes the four relationals; == is NOT
    # synthesized (C++ itself only rewrites == from operator==, and Temp declares
    # none) — equality falls back to Python identity.
    assert ops.Temp(1.0) < ops.Temp(2.0)
    assert ops.Temp(2.0) > ops.Temp(1.0)
    assert ops.Temp(1.0) <= ops.Temp(1.0)
    assert ops.Temp(1.0) >= ops.Temp(1.0)
    t = ops.Temp(1.0)
    assert t == t  # identity
    assert ops.Temp(1.0) != ops.Temp(1.0)  # no C++ ==: distinct objects differ


def test_heterogeneous_spaceship_both_directions(ops: ModuleType) -> None:
    # <=>(int) synthesizes int-operand comparisons; the reversed direction rides
    # Python's reflected protocol (5 < a -> a.__gt__(5)), which works because the
    # synthesized slots return NotImplemented on an operand mismatch.
    a = ops.Account(10)
    assert a < 20
    assert a > 5
    assert 5 < a
    assert 20 > a
    assert a == 10  # the member operator==(int)
    assert a != 11


def test_explicit_operator_beats_synthesis(ops: ModuleType) -> None:
    # Ordered declares an (inverted) operator< AND a <=>: the explicit operator
    # wins its slot, the rest synthesize — exactly what a C++ caller sees.
    assert ops.Ordered(2) < ops.Ordered(1)  # the inverted explicit <
    assert ops.Ordered(2) > ops.Ordered(1)  # synthesized from <=>
    assert ops.Ordered(1) <= ops.Ordered(2)  # synthesized from <=>


def test_spaceship_marks_scope_per_language(ops: ModuleType) -> None:
    # PyOnlyCmp's <=> is exclude(lua)'d: Python still synthesizes (the Lua specs
    # assert the absence on their side).
    assert ops.PyOnlyCmp(1) < ops.PyOnlyCmp(2)


# --- exclude/include marks on operators resolve like they do on methods. ---


def test_operator_excluded_under_automatic(ops: ModuleType) -> None:
    # Under automatic policy everything binds unless excluded: operator+ surfaces
    # as __add__, but the mark::exclude'd operator* does not surface as __mul__.
    assert hasattr(ops.OpAutomatic, "__add__")
    assert not hasattr(ops.OpAutomatic, "__mul__")
    assert (ops.OpAutomatic(3) + ops.OpAutomatic(4)).v == 7


def test_operator_opt_in_requires_include(ops: ModuleType) -> None:
    # Under opt_in policy an operator binds only when explicitly included: the
    # include'd operator+ surfaces as __add__, the unmarked operator- does not
    # surface as __sub__.
    assert hasattr(ops.OpOptIn, "__add__")
    assert not hasattr(ops.OpOptIn, "__sub__")
    assert (ops.OpOptIn(3) + ops.OpOptIn(4)).v == 7
