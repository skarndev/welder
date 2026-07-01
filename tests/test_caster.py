"""Tests for a member/parameter whose type carries a self-contained pybind11
type_caster.

welder binds such a type *automatically* — no weld, no trust_bindable — because a
specialized ``type_caster<T>`` displaces pybind11's generic class-registration
fallback, so welder's bindability gate sees it as native. The type presents as its
caster's Python form (here a ``float``). C++ side: tests/pybind11/cpp/caster.hpp.
"""

from __future__ import annotations

from types import ModuleType

from conftest import public_attrs


def test_custom_caster_member_binds_and_roundtrips(mod: ModuleType) -> None:
    obj = mod.Sample()
    assert {"temperature", "sensor"} <= public_attrs(obj)
    # the member reads/writes as a plain float, through the caster
    obj.temperature = 21.5
    assert obj.temperature == 21.5


def test_custom_caster_in_method_signature(mod: ModuleType) -> None:
    obj = mod.Sample()
    obj.temperature = 20.0
    # parameter and return both go through the caster (float in, float out)
    assert obj.warmer(1.5) == 21.5
