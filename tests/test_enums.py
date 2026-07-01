"""Tests for welder binding C++ enums as py::enum_.

A welded enum's enumerators resolve like data members (the enum's policy + each
enumerator's exclude/include marks). Scoped enums are reached as ``E.Value``; an
unscoped enum also exports its values unqualified into the module, mirroring C++.
C++ side: tests/pybind11/cpp/enums.hpp.
"""

from __future__ import annotations

from types import ModuleType


# --- scoped enum, automatic policy ------------------------------------------
def test_scoped_enum_binds_its_values(mod: ModuleType) -> None:
    assert int(mod.Direction.North) == 0
    assert int(mod.Direction.East) == 1
    # West keeps its C++ value (3), even though South (2) was excluded from binding.
    assert int(mod.Direction.West) == 3


def test_excluded_enumerator_is_not_bound(mod: ModuleType) -> None:
    assert not hasattr(mod.Direction, "South")


# --- unscoped enum: values exported into the module -------------------------
def test_unscoped_enum_exports_values_to_module(mod: ModuleType) -> None:
    assert hasattr(mod, "Green") and hasattr(mod, "Yellow") and hasattr(mod, "Red")
    assert mod.Green == mod.Signal.Green
    assert int(mod.Signal.Red) == 2


# --- scoped enum, opt_in policy ---------------------------------------------
def test_opt_in_enum_binds_only_included_values(mod: ModuleType) -> None:
    assert hasattr(mod.Level, "Debug")
    assert hasattr(mod.Level, "Info")
    assert not hasattr(mod.Level, "Trace")


# --- an enum-typed struct member --------------------------------------------
def test_enum_typed_member_roundtrips(mod: ModuleType) -> None:
    compass = mod.Compass()
    compass.facing = mod.Direction.West
    assert compass.facing == mod.Direction.West
