"""Tests for welder's handling of inheritance, observed through the generated
module. welder splits public bases two ways:

* a base that is itself welded becomes a *native* pybind11 base — the derived
  type is a real Python subclass and inherits its members through the MRO;
* a non-welded base is a plain C++ *mixin* whose eligible members are *flattened*
  directly onto each derived binding (no Python subclass relationship).
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def inh(mod: ModuleType) -> ModuleType:
    # The cases bind under the `inheritance` submodule (C++ side: namespace `inheritance`).
    return cast(ModuleType, mod.inheritance)


# --- native inheritance from a welded base ----------------------------------
def test_welded_base_is_a_python_superclass(inh: ModuleType) -> None:
    assert issubclass(inh.Derived, inh.Base)


def test_derived_inherits_base_members(inh: ModuleType) -> None:
    d = inh.Derived()
    # inherited from Base (through the MRO, not re-bound on Derived)...
    assert d.base_field == 1
    assert d.base_method() == 1
    # ...alongside its own members.
    assert d.derived_field == 2
    assert d.derived_method() == 2


def test_excluded_base_member_is_never_bound(inh: ModuleType) -> None:
    assert "base_secret" not in public_attrs(inh.Base())
    assert "base_secret" not in public_attrs(inh.Derived())


@pytest.mark.parametrize(
    ("superclass",),
    [pytest.param("Mid", id="direct-base"), pytest.param("Base", id="grandparent")],
)
def test_multilevel_welded_chain(inh: ModuleType, superclass: str) -> None:
    assert issubclass(inh.Leaf, getattr(inh, superclass))


def test_leaf_sees_every_level_of_the_chain(inh: ModuleType) -> None:
    attrs = public_attrs(inh.Leaf())
    assert {"base_field", "mid_field", "leaf_field"} <= attrs


# --- flattening of a non-welded mixin base ----------------------------------
def test_mixin_members_are_flattened_in(inh: ModuleType) -> None:
    w = inh.WithMixin()
    assert w.mixin_field == 3
    assert w.mixin_method() == 3
    assert w.own_field == 4


def test_mixin_marks_are_honored_when_flattened(inh: ModuleType) -> None:
    assert "mixin_secret" not in public_attrs(inh.WithMixin())


def test_non_welded_mixin_is_not_a_python_class(inh: ModuleType) -> None:
    # The mixin is folded in, not exposed as its own bound type.
    assert not hasattr(inh, "Mixin")
    assert inh.WithMixin.__bases__ == (inh.WithMixin.__mro__[1],)
    assert inh.WithMixin.__mro__[1].__name__ != "Mixin"


# --- a welded base reached only through a non-welded base --------------------
def test_welded_base_links_through_a_non_welded_bridge(inh: ModuleType) -> None:
    # `weld` survives the non-welded bridge: Through is still a subclass of Welded.
    assert issubclass(inh.Through, inh.Welded)
    assert not hasattr(inh, "Bridge")


def test_through_sees_welded_base_and_flattened_bridge(inh: ModuleType) -> None:
    t = inh.Through()
    assert t.welded_field == 10        # inherited natively from Welded
    assert t.welded_method() == 10
    assert t.bridge_field == 11        # flattened from the non-welded bridge
    assert t.through_field == 12


# --- a virtual diamond, all welded ------------------------------------------
# A diamond needs two native base classes. Backends that bind only single
# inheritance (e.g. nanobind) omit the diamond types entirely, so these cases skip
# when `Bottom` is absent rather than asserting a backend name.
def _require_diamond(inh: ModuleType) -> None:
    if not hasattr(inh, "Bottom"):
        pytest.skip("backend does not bind multiple inheritance")


@pytest.mark.parametrize(
    ("superclass",),
    [
        pytest.param("Left", id="left-arm"),
        pytest.param("Right", id="right-arm"),
        pytest.param("Apex", id="shared-virtual-base"),
    ],
)
def test_diamond_subclassing(inh: ModuleType, superclass: str) -> None:
    _require_diamond(inh)
    assert issubclass(inh.Bottom, getattr(inh, superclass))


def test_diamond_exposes_every_field_once(inh: ModuleType) -> None:
    _require_diamond(inh)
    b = inh.Bottom()
    assert (b.apex_field, b.left_field, b.right_field, b.bottom_field) == (20, 21, 22, 23)
