"""Tests for welder binding class-NESTED types (member classes and enums).

A type nested inside a welded class resolves like any other class member — the
outer's policy plus the nested type's own exclude/include/only marks; it never
carries a ``weld`` of its own. The Python rods register it with the enclosing
class as the scope, so it appears as ``module.Outer.Inner`` (with a nested
``__qualname__``), exactly like a hand-written
``py::class_<Outer::Inner>(outer_cls, "Inner")``.
C++ side: tests/common/cpp/nested.hpp.
"""

from __future__ import annotations

import enum
from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def nested(mod: ModuleType) -> ModuleType:
    # The cases bind under the `nested` submodule (C++ side: namespace `nested`).
    return cast(ModuleType, mod.nested)


# --- a nested class binds under the outer's binding --------------------------
def test_nested_class_is_an_attribute_of_the_outer(nested: ModuleType) -> None:
    assert isinstance(nested.Robot.Sensor, type)
    # scoped under the class, not a module-level sibling
    assert not hasattr(nested, "Sensor")
    assert nested.Robot.Sensor.__qualname__ == "Robot.Sensor"


def test_nested_class_is_fully_bound(nested: ModuleType) -> None:
    s = nested.Robot.Sensor()
    assert s.range == pytest.approx(1.5)
    s.range = 2.0
    assert s.doubled() == pytest.approx(4.0)
    # the synthesized aggregate field constructor works for nested types too
    assert nested.Robot.Sensor(3.0).doubled() == pytest.approx(6.0)


def test_members_typed_with_nested_types_round_trip(nested: ModuleType) -> None:
    r = nested.Robot()
    assert r.sensor.range == pytest.approx(1.5)
    r.set_mode(nested.Robot.Mode.active)
    assert r.get_mode() == nested.Robot.Mode.active
    assert r.mode == nested.Robot.Mode.active


# --- nested enums -------------------------------------------------------------
def test_nested_scoped_enum_binds_under_the_class(nested: ModuleType) -> None:
    assert issubclass(nested.Robot.Mode, enum.IntEnum)
    assert int(nested.Robot.Mode.fault) == 2
    # scoped: values stay qualified
    assert not hasattr(nested.Robot, "fault")


def test_nested_unscoped_enum_exports_values_onto_the_class(nested: ModuleType) -> None:
    # mirrors C++: Robot::quiet is spelled without naming Alarm
    assert issubclass(nested.Robot.Alarm, enum.IntEnum)
    assert nested.Robot.quiet == nested.Robot.Alarm.quiet
    assert int(nested.Robot.loud) == 1


def test_method_using_nested_enums(nested: ModuleType) -> None:
    r = nested.Robot()
    assert r.alarm_for(nested.Robot.Mode.fault) == nested.Robot.Alarm.loud
    assert r.alarm_for(nested.Robot.Mode.idle) == nested.Robot.Alarm.quiet


# --- marks on nested types ------------------------------------------------------
def test_excluded_nested_type_is_not_bound(nested: ModuleType) -> None:
    assert not hasattr(nested.Robot, "Hidden")


def test_language_scoped_exclude(nested: ModuleType) -> None:
    # excluded for lua only -> Python still binds it
    assert nested.Robot.Config().level == 3


# --- recursion: nested-in-nested -------------------------------------------------
def test_nesting_recurses(nested: ModuleType) -> None:
    assert nested.Machine.Part.Bolt.__qualname__ == "Machine.Part.Bolt"
    m = nested.Machine()
    assert m.part.bolt.size == 5
    assert nested.Machine.Part.Bolt(9).size == 9


# --- opt_in outer -----------------------------------------------------------------
def test_opt_in_binds_only_included_nested_types(nested: ModuleType) -> None:
    assert nested.Panel.Knob().pos == 0
    assert not hasattr(nested.Panel, "Wiring")


# --- access ------------------------------------------------------------------------
def test_private_nested_type_never_binds(nested: ModuleType) -> None:
    assert nested.Cabinet().drawers == 2
    assert not hasattr(nested.Cabinet, "Stash")