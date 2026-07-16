"""Tests for the copy-constructor binding (Python's copy protocol).

The copy constructor never binds as an init overload; it becomes the visible
copy constructor ``T(other)`` plus the copy protocol — ``__copy__`` and
``__deepcopy__(memo)``, both **subclass-faithful**: like Python's own copy
machinery they transfer state without calling ``__init__`` (a
``type(self).__new__`` shell, the C++ payload copy-constructed in place, the
instance ``__dict__`` carried over — deep-copied through the memo on the
deepcopy path), so a Python subclass copies as itself, overrides and
attributes intact. Admission mirrors the default constructor's: an implicit
copy constructor rides along whenever the type is copy-constructible, a
declared one's explicit marks are honored (per language when scoped), and
opt_in's default-out does not apply. Move constructors never bind — an
include/only mark on one is a designed hard error (locked by
``negcompile.move_ctor_marked``).
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


# --- the visible copy constructor: T(other) ------------------------------------
def test_copy_construction_from_python(cp: ModuleType) -> None:
    s = cp.Sheet()
    s.width = 4
    assert cp.Sheet(s).width == 4


# --- Python subclasses copy as themselves --------------------------------------
def test_subclass_copy_is_faithful(cp: ModuleType) -> None:
    # State transfer, never __init__ (exactly like Python's default copy
    # machinery): the subclass __init__ below has a different signature and
    # must NOT be re-run; the attributes it set carry over via __dict__.
    class Ledger(cp.Sheet):  # type: ignore[misc, name-defined]
        def __init__(self) -> None:
            super().__init__()
            self.notes = ["a"]

    a = Ledger()
    a.width = 7
    c = copy.copy(a)
    assert type(c) is Ledger
    assert c.width == 7 and c.notes == ["a"]
    # __copy__ is shallow: the Python-side attribute is shared, deepcopy's isn't
    assert c.notes is a.notes
    d = copy.deepcopy(a)
    assert type(d) is Ledger and d.notes == ["a"] and d.notes is not a.notes


def test_deepcopy_memo_dedups_and_terminates_cycles(cp: ModuleType) -> None:
    class Ledger(cp.Sheet):  # type: ignore[misc, name-defined]
        pass

    a = Ledger()
    a.buddy = a  # a reference cycle through the instance __dict__
    d = copy.deepcopy(a)
    assert d.buddy is d
    pair = copy.deepcopy([a, a])
    assert pair[0] is pair[1]


# --- polymorphic types: the copy keeps dispatching into Python -----------------
def test_virtual_type_carries_the_copy_protocol(cp: ModuleType) -> None:
    b = cp.Brush()
    b.width = 3
    c = copy.copy(b)
    assert type(c) is cp.Brush and c.width == 3
    c.width = 9
    assert b.width == 3


def test_copying_a_python_subclass_keeps_the_overrides(cp: ModuleType) -> None:
    # The trampoline's copy-from-base constructor lets the backend build the
    # ALIAS payload on the subclass shell, so C++ virtual calls on the copy
    # still dispatch into the Python override — no slicing.
    class Dotted(cp.Brush):  # type: ignore[misc, name-defined]
        def stroke(self) -> str:
            return "dotted"

    d = Dotted()
    d.width = 7
    assert d.paint() == "paint:dotted"  # C++ dispatches into the override
    c = copy.copy(d)
    assert type(c) is Dotted
    assert c.width == 7
    assert c.paint() == "paint:dotted"  # ... and so does the copy
    c.width = 100
    assert d.width == 7


def test_abstract_type_binds_no_protocol(cp: ModuleType) -> None:
    # Not copy-constructible (pure virtual), so no protocol — and no error.
    assert not hasattr(cp.Stencil, "__copy__")
    assert not hasattr(cp.Stencil, "__deepcopy__")


# --- tack welding: admission never depended on marks ---------------------------
def test_tack_welded_types_carry_the_copy_protocol(mod: ModuleType) -> None:
    # The implicit copy constructor rides along under the greedy resolution
    # exactly as under the marker one — copy-constructibility alone decides
    # (foreign.Widget is tack-welded from an unmarked namespace; see
    # namespace.hpp / test_namespace.py).
    w = mod.foreign.Widget()
    w.size = 5
    assert copy.copy(w).size == 5