"""Tests for binding overloaded operators as Python special methods.

A member operator on a welded type is exposed under its matching dunder
(operator+ -> __add__, operator== -> __eq__, ...). See tests/pybind11/cpp/operators.hpp.
"""

from __future__ import annotations

from types import ModuleType


def test_binary_add(mod: ModuleType) -> None:
    r = mod.Vec(1.0, 2.0) + mod.Vec(3.0, 4.0)
    assert (r.x, r.y) == (4.0, 6.0)


def test_binary_sub(mod: ModuleType) -> None:
    r = mod.Vec(5.0, 7.0) - mod.Vec(1.0, 2.0)
    assert (r.x, r.y) == (4.0, 5.0)


def test_unary_neg(mod: ModuleType) -> None:
    # operator-() (0 params) binds to __neg__, distinct from binary operator-.
    r = -mod.Vec(1.0, -2.0)
    assert (r.x, r.y) == (-1.0, 2.0)


def test_scalar_mul(mod: ModuleType) -> None:
    r = mod.Vec(1.5, 2.0) * 2.0
    assert (r.x, r.y) == (3.0, 4.0)


def test_equality(mod: ModuleType) -> None:
    assert mod.Vec(1.0, 2.0) == mod.Vec(1.0, 2.0)
    assert mod.Vec(1.0, 2.0) != mod.Vec(3.0, 4.0)


def test_subscript(mod: ModuleType) -> None:
    v = mod.Vec(1.0, 2.0)
    assert (v[0], v[1]) == (1.0, 2.0)


def test_assignment_operator_is_not_bound(mod: ModuleType) -> None:
    # operator= is a special member; it must not surface as a Python method.
    assert not hasattr(mod.Vec, "__assign__")
