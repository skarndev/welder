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


def test_free_operator_is_not_bound(ops: ModuleType) -> None:
    # Coin's operator+ is a free (non-member) function defined separately from the
    # class. welder only scans a type's own members, so the free operator is not
    # discovered: __add__ is absent and Coin + Coin is unsupported.
    assert not hasattr(ops.Coin, "__add__")
    with pytest.raises(TypeError):
        ops.Coin(1) + ops.Coin(2)


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
