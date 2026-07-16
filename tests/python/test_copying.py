"""Tests for the copy-constructor binding (Python's copy protocol).

The copy constructor never binds as an init overload; the Python rods spell it
as ``__copy__`` and ``__deepcopy__(memo)``, both delegating to the C++ copy
constructor. Admission mirrors the default constructor's: an implicit copy
constructor rides along whenever the type is copy-constructible, a declared
one's explicit marks are honored (per language when scoped), and opt_in's
default-out does not apply. Move constructors never bind — an include/only
mark on one is a designed hard error (locked by ``negcompile.move_ctor_marked``).
C++ side: tests/common/cpp/copying.hpp.
"""

from __future__ import annotations

import copy
from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def cp(mod: ModuleType) -> ModuleType:
    # The cases bind under the `copying` submodule (C++ side: namespace `copying`).
    return cast(ModuleType, mod.copying)


# --- implicit copy constructor: the copy protocol rides along -----------------
def test_copy_protocol_is_bound(cp: ModuleType) -> None:
    assert hasattr(cp.Sheet, "__copy__")
    assert hasattr(cp.Sheet, "__deepcopy__")


def test_copy_produces_an_independent_object(cp: ModuleType) -> None:
    s = cp.Sheet()
    s.width = 3
    s.title = "original"
    c = copy.copy(s)
    assert (c.width, c.title) == (3, "original")
    c.width = 9
    c.title = "copy"
    assert (s.width, s.title) == (3, "original")


def test_deepcopy_produces_an_independent_object(cp: ModuleType) -> None:
    s = cp.Sheet()
    s.width = 5
    d = copy.deepcopy(s)
    assert d.width == 5
    d.width = 11
    assert s.width == 5


# --- deleted copy: not copy-constructible -> no copy protocol -----------------
def test_deleted_copy_binds_no_protocol(cp: ModuleType) -> None:
    assert not hasattr(cp.Pinned, "__copy__")
    assert not hasattr(cp.Pinned, "__deepcopy__")
    # copy.copy falls back to the pickle protocol, which the binding lacks too.
    with pytest.raises(TypeError):
        copy.copy(cp.Pinned())


# --- a declared copy constructor's marks are honored ---------------------------
def test_excluded_copy_binds_no_protocol(cp: ModuleType) -> None:
    assert not hasattr(cp.Sealed, "__copy__")
    assert not hasattr(cp.Sealed, "__deepcopy__")


def test_per_language_exclude_suppresses_for_py(cp: ModuleType) -> None:
    # exclude(lang::py) on the copy constructor: no Python copy protocol (the
    # Lua rods ignore the copy flag regardless — nothing to observe there).
    assert not hasattr(cp.PyBlocked, "__copy__")
    assert not hasattr(cp.PyBlocked, "__deepcopy__")


# --- opt_in: a declared, unmarked copy constructor stays admitted -------------
def test_optin_default_out_does_not_apply(cp: ModuleType) -> None:
    # The mirror of the default constructor's opt_in exemption: an unmarked
    # `Choosy(const Choosy&) = default;` still binds the copy protocol.
    c = cp.Choosy()
    c.chosen = 8
    assert copy.copy(c).chosen == 8


# --- a declared move constructor is skipped structurally ----------------------
def test_move_ctor_is_silently_skipped(cp: ModuleType) -> None:
    # The move constructor binds nothing (and errors nothing, unmarked); the
    # copy protocol is untouched.
    s = cp.Shifty()
    s.n = 6
    assert copy.copy(s).n == 6