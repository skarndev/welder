"""Tests for welder binding C++ enums as a stdlib enum.IntEnum.

A welded enum's enumerators resolve like data members (the enum's policy + each
enumerator's exclude/include marks). Scoped enums are reached as ``E.Value``; an
unscoped enum also exports its values unqualified into the module, mirroring C++.
C++ side: tests/pybind11/cpp/enums.hpp.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def enums(mod: ModuleType) -> ModuleType:
    # The cases bind under the `enums` submodule (C++ side: namespace `enums`).
    return cast(ModuleType, mod.enums)


# --- scoped enum, automatic policy ------------------------------------------
def test_scoped_enum_binds_its_values(enums: ModuleType) -> None:
    assert int(enums.Direction.North) == 0
    assert int(enums.Direction.East) == 1
    # West keeps its C++ value (3), even though South (2) was excluded from binding.
    assert int(enums.Direction.West) == 3


def test_excluded_enumerator_is_not_bound(enums: ModuleType) -> None:
    assert not hasattr(enums.Direction, "South")


# --- unscoped enum: values exported into the (sub)module --------------------
def test_unscoped_enum_exports_values_to_module(enums: ModuleType) -> None:
    # export_values() lands the enumerators in the enclosing scope — now the
    # `enums` submodule, mirroring how C++ exports them into their namespace.
    assert hasattr(enums, "Green") and hasattr(enums, "Yellow") and hasattr(enums, "Red")
    assert enums.Green == enums.Signal.Green
    assert int(enums.Signal.Red) == 2


# --- scoped enum, opt_in policy ---------------------------------------------
def test_opt_in_enum_binds_only_included_values(enums: ModuleType) -> None:
    assert hasattr(enums.Level, "Debug")
    assert hasattr(enums.Level, "Info")
    assert not hasattr(enums.Level, "Trace")


# --- an enum-typed struct member --------------------------------------------
def test_enum_typed_member_roundtrips(enums: ModuleType) -> None:
    compass = enums.Compass()
    compass.facing = enums.Direction.West
    assert compass.facing == enums.Direction.West


# --- flags (bitmask) enums ---------------------------------------------------
def test_flags_enum_binds_an_intflag(enums: ModuleType) -> None:
    import enum

    # [[=welder::flags]] -> enum.IntFlag: combining stays inside the type.
    assert issubclass(enums.Styles, enum.IntFlag)
    v = enums.Styles.Bold | enums.Styles.Italic
    assert isinstance(v, enums.Styles)
    assert int(v) == 0x3


def test_flags_enum_accepts_combined_and_unknown_bits(enums: ModuleType) -> None:
    # The C++ default carries Bold plus an undocumented 0x80 bit — a plain
    # IntEnum would raise ValueError converting it; IntFlag keeps the bits.
    t = enums.Text()
    assert int(t.styles) == 0x81
    assert enums.Styles.Bold in t.styles

    t.styles = enums.Styles.Bold | enums.Styles.Underline
    assert int(t.styles) == 0x5


def test_plain_enum_still_rejects_combined_values(enums: ModuleType) -> None:
    # The IntEnum behavior of unmarked enums is unchanged.
    with pytest.raises(ValueError):
        enums.Direction(0x1234)
