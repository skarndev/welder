"""Tests for the opaque-container GENERATOR (welder::rods::opaque_containers).

Unlike test_opaque.py — where the WELDER_OPAQUE declarations and welded aliases are
hand-written — here none of that boilerplate exists: the C++ types (gen_opaque.hpp)
carry plain ``std::vector`` / ``std::map`` members and signatures, and a build-time
generator reflects them and emits the header that binds those containers by
reference. These specs assert the generated result behaves: a member is the bound
wrapper with write-through, a ``by_value``-marked member stays a list, and a
container surfaced only through a signature is bound too. Runs against both rods
(one generated header, shared). C++ side: tests/common/cpp/gen_opaque.hpp.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def go(mod: ModuleType) -> ModuleType:
    return cast(ModuleType, mod.gen_opaque)


def test_generated_aliases_exist(go: ModuleType) -> None:
    # the generator derived these names from the containers it found
    assert hasattr(go, "VectorDouble")
    # a welded-CLASS element container is opened too (two-phase name pre-registration)
    assert hasattr(go, "VectorReading")
    # vector<int> was opted out with by_value, so no wrapper was generated for it
    assert not hasattr(go, "VectorInt")


def test_member_is_opaque_and_writes_through(go: ModuleType) -> None:
    s = go.Series()
    assert isinstance(s.points, go.VectorDouble)
    assert not isinstance(s.points, list)
    s.points.append(2.0)
    s.points.append(3.0)
    assert go.sum_points(s) == pytest.approx(5.0)  # reached the C++ vector
    s.points[0] = 10.0
    assert go.sum_points(s) == pytest.approx(13.0)


def test_by_value_member_stays_a_list(go: ModuleType) -> None:
    s = go.Series()
    assert isinstance(s.raw, list)  # opted out of the generator -> plain list[int]
    s.raw = [1, 2, 3]
    assert s.raw == [1, 2, 3]


def test_class_element_container_member_writes_through(go: ModuleType) -> None:
    # vector<Reading> (welded-class element) is opened OPAQUE — a member is the bound
    # wrapper, and appends write through to the C++ vector (aggregate-NSDMI field).
    s = go.Series()
    assert isinstance(s.readings, go.VectorReading)
    assert not isinstance(s.readings, list)
    s.readings.append(go.Reading(1.0))
    s.readings.append(go.Reading(2.0))
    assert len(s.readings) == 2
    assert s.readings[1].value == pytest.approx(2.0)


def test_class_element_container_from_a_signature(go: ModuleType) -> None:
    # a vector<Reading>-returning function returns the opaque wrapper, not a list
    readings = go.take(3)
    assert isinstance(readings, go.VectorReading)
    assert len(readings) == 3
    assert [r.value for r in readings] == pytest.approx([0.0, 1.0, 2.0])
    assert all(isinstance(r, go.Reading) for r in readings)
