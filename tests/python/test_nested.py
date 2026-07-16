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


def test_protected_nested_type_binds_under_weld_protected(nested: ModuleType) -> None:
    assert nested.Rig().id == 1
    assert nested.Rig.Jig().slots == 7


def test_exclude_plus_weld_is_the_manual_flat_escape(nested: ModuleType) -> None:
    # Robot::Beacon is excluded from the sweep and carries its own weld; the
    # explicit weld_type call registers it once, flat, under a chosen name.
    assert not hasattr(nested.Robot, "Beacon")
    assert nested.RobotBeacon().strength == 9


def test_unregistrable_member_types_are_skipped_silently(nested: ModuleType) -> None:
    # a forward-declared member type and a union: never swept, never an error
    assert not hasattr(nested.Robot, "Probe")
    assert not hasattr(nested.Robot, "Blob")


# --- member type aliases ------------------------------------------------------------
def test_member_alias_registers_an_unwelded_target(nested: ModuleType) -> None:
    # A member alias participates iff the target FAILS the bindability gate —
    # exactly the types that otherwise couldn't cross the boundary — registered
    # nested under the outer, named by the alias.
    assert nested.Console.Dial.__qualname__ == "Console.Dial"
    assert nested.Console.Dial().reading == 40
    assert nested.Console.Ints().take() == 0  # an unnameable specialization
    import enum

    assert issubclass(nested.Console.Lvl, enum.IntEnum)  # a vendor enum


def test_member_alias_weld_as(nested: ModuleType) -> None:
    assert nested.Console.Spool().take() == pytest.approx(0.0)
    assert not hasattr(nested.Console, "Reel")


def test_gate_passing_targets_are_skipped(nested: ModuleType) -> None:
    assert not hasattr(nested.Console, "Names")  # castable (vector<string>)
    assert not hasattr(nested.Console, "Bot")    # welded (Robot) — no double reg
    assert nested.Robot().get_mode() is not None  # Robot itself unharmed


def test_excluded_member_alias_never_participates(nested: ModuleType) -> None:
    assert not hasattr(nested.Console, "Gauge")


def test_exclude_plus_alias_renames_a_declared_nested_type(nested: ModuleType) -> None:
    assert not hasattr(nested.Console, "Core")  # excluded from the sweep
    assert nested.Console.Heart().temp == 300   # re-registered under the alias


def test_scope_aware_gate_admits_alias_registered_types(nested: ModuleType) -> None:
    # Members whose signatures use the alias-registered vendor types bind: the
    # class's own member aliases are visible to the registration oracle.
    c = nested.Console()
    assert c.dial.reading == 40
    assert c.read_dial().reading == 40
    assert c.spin().take() == 0
    assert c.level() == nested.Console.Lvl.high
