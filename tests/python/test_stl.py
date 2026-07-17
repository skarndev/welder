"""Tests for STL-container conversions across the binding boundary.

The conversions are the framework casters' work (pybind11/stl.h, nanobind's
stl/*.h); these specs pin down the surface welder manufactures on top of them:
a ``std::vector`` parameter accepts a Python list, a container return arrives
as the native ``list``/``dict``/``tuple``, ``std::optional`` round-trips
``None``, and container-typed data members read and write as native objects.
The complementary *type-level* assertions (Sequence[T] parameters, list[T]
returns in the generated stubs) live in test_types.mypy-testing.
C++ side: tests/common/cpp/stl.hpp.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def stl(mod: ModuleType) -> ModuleType:
    # The cases bind under the `stl` submodule (C++ side: namespace `stl`).
    return cast(ModuleType, mod.stl)


# --- vector<T> <-> list ------------------------------------------------------
def test_vector_return_is_a_list(stl: ModuleType) -> None:
    assert stl.iota(4) == [0, 1, 2, 3]
    assert isinstance(stl.iota(0), list)


def test_vector_parameter_accepts_a_list(stl: ModuleType) -> None:
    assert stl.total([1, 2, 3]) == 6
    # any sequence converts, not just list
    assert stl.total((4, 5)) == 9


def test_vector_of_welded_type_round_trips(stl: ModuleType) -> None:
    items = stl.wrap_all([7, 9])
    assert [it.id for it in items] == [7, 9]
    assert all(isinstance(it, stl.Item) for it in items)


# --- map<K, V> <-> dict ------------------------------------------------------
def test_map_return_is_a_dict(stl: ModuleType) -> None:
    assert stl.histogram(["a", "b", "a"]) == {"a": 2, "b": 1}


# --- optional<T> <-> T | None ------------------------------------------------
def test_optional_return_is_value_or_none(stl: ModuleType) -> None:
    assert stl.find_positive([-1, 0, 5]) == 5
    assert stl.find_positive([-1, 0]) is None


def test_optional_parameter_accepts_none(stl: ModuleType) -> None:
    assert stl.value_or(3, 8) == 3
    assert stl.value_or(None, 8) == 8


# --- pair <-> tuple ----------------------------------------------------------
def test_pair_return_is_a_tuple(stl: ModuleType) -> None:
    assert stl.labelled(5) == (5, "5")


# --- container-typed data members --------------------------------------------
def test_container_members_read_and_write(stl: ModuleType) -> None:
    b = stl.Basket()
    assert b.ints == [] and b.ranks == {} and b.discount is None
    b.ints = [1, 2]
    b.ranks = {"gold": 1}
    b.discount = 0.5
    assert b.ints == [1, 2]
    assert b.ranks == {"gold": 1}
    assert b.discount == 0.5
    b.discount = None
    assert b.discount is None