"""Tests for welder::pybind11::bind_namespace — exposing a whole namespace at once.

The C++ namespace ``catalog`` is bound under the ``catalog`` submodule of the test
module. welder walks it and exposes every member carrying a ``weld``: classes,
free functions, namespace-scope variables, and nested namespaces (as submodules).
"""

from __future__ import annotations

from types import ModuleType

import pytest

from conftest import public_attrs


@pytest.fixture()
def cat(mod: ModuleType) -> ModuleType:
    return mod.catalog


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
    cat.counter = 500
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
