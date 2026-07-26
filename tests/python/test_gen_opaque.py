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
    # the generator derived these names from the containers it found. Names are
    # collision-free QUALIFIED: a scalar keeps its short name (VectorDouble), but a
    # welded element carries its namespace path (gen_opaque::Reading -> GenOpaqueReading)
    # so two same-named types in different namespaces never collide.
    assert hasattr(go, "VectorDouble")
    # a welded-CLASS element container is opened too (two-phase name pre-registration),
    # named by its qualified path
    assert hasattr(go, "VectorGenOpaqueReading")
    # a class-TEMPLATE element (Layer<0>, an NTTP arg) gets a valid derived name — this
    # used to be invalid C++ (a name with '<' '>' '::')
    assert hasattr(go, "VectorGenOpaqueLayer0")
    # vector<int> was opted out with by_value, so no wrapper was generated for it
    assert not hasattr(go, "VectorInt")


def test_template_instantiation_element_binds_opaque(go: ModuleType) -> None:
    s = go.Series()
    assert isinstance(s.tiers, go.VectorGenOpaqueLayer0)  # opaque, not a list
    s.tiers.append(go.Layer0())
    s.tiers.append(go.Layer0())
    assert len(s.tiers) == 2  # append is push_back on the live C++ vector


def test_member_is_opaque_and_writes_through(go: ModuleType) -> None:
    s = go.Series()
    assert isinstance(s.points, go.VectorDouble)
    assert not isinstance(s.points, list)
    s.points.append(2.0)
    s.points.append(3.0)
    assert go.sum_points(s) == pytest.approx(5.0)  # reached the C++ vector
    s.points[0] = 10.0
    assert go.sum_points(s) == pytest.approx(13.0)


def test_nested_container_member_is_opaque_both_levels(go: ModuleType) -> None:
    # vector<vector<double>>: the generator opened BOTH levels — the member is the
    # outer wrapper and each row is a live inner wrapper, so mutation through either
    # level reaches the C++ object.
    s = go.Series()
    assert hasattr(go, "VectorVectorDouble")
    assert isinstance(s.grid, go.VectorVectorDouble)
    s.grid.append(go.VectorDouble())
    row = s.grid[0]
    assert isinstance(row, go.VectorDouble)
    row.append(2.0)
    row.append(3.0)
    assert go.sum_grid(s) == pytest.approx(5.0)  # reached the C++ rows
    s.grid[0][0] = 10.0
    assert go.sum_grid(s) == pytest.approx(13.0)


def test_by_value_member_stays_a_list(go: ModuleType) -> None:
    s = go.Series()
    assert isinstance(s.raw, list)  # opted out of the generator -> plain list[int]
    s.raw = [1, 2, 3]
    assert s.raw == [1, 2, 3]


def test_class_element_container_member_writes_through(go: ModuleType) -> None:
    # vector<Reading> (welded-class element) is opened OPAQUE — a member is the bound
    # wrapper, and appends write through to the C++ vector (aggregate-NSDMI field).
    s = go.Series()
    assert isinstance(s.readings, go.VectorGenOpaqueReading)
    assert not isinstance(s.readings, list)
    s.readings.append(go.Reading(1.0))
    s.readings.append(go.Reading(2.0))
    assert len(s.readings) == 2
    assert s.readings[1].value == pytest.approx(2.0)


def test_class_element_container_from_a_signature(go: ModuleType) -> None:
    # a vector<Reading>-returning function returns the opaque wrapper, not a list
    readings = go.take(3)
    assert isinstance(readings, go.VectorGenOpaqueReading)
    assert len(readings) == 3
    assert [r.value for r in readings] == pytest.approx([0.0, 1.0, 2.0])
    assert all(isinstance(r, go.Reading) for r in readings)


def test_new_default_constructs_a_class_element_and_writes_through(go: ModuleType) -> None:
    # new() grows the C++ vector by a default-constructed element and hands back a
    # LIVE reference to it — no need to import the element type to construct one.
    s = go.Series()
    e = s.readings.new()
    assert isinstance(e, go.Reading)
    assert e.value == pytest.approx(0.0)  # default-constructed (NSDMI value{0.0})
    assert len(s.readings) == 1
    e.value = 7.0  # mutation writes through the live reference
    assert s.readings[0].value == pytest.approx(7.0)


def test_new_on_template_instantiation_element(go: ModuleType) -> None:
    # works for a class-template-specialization element (Layer<0>) too
    s = go.Series()
    layer = s.tiers.new()
    assert isinstance(layer, go.Layer0)
    assert layer.depth == 0
    assert len(s.tiers) == 1


def test_new_absent_on_scalar_element_container(go: ModuleType) -> None:
    # scalars would come back by value (a dead copy), a footgun — new() is omitted;
    # append(value) is the scalar path.
    s = go.Series()
    assert not hasattr(s.points, "new")


def test_resize_value_initializes_new_elements(go: ModuleType) -> None:
    # resize(n) grows to exactly n, value-initializing the new tail (Reading.value{0.0})
    s = go.Series()
    s.readings.resize(3)
    assert len(s.readings) == 3
    assert [r.value for r in s.readings] == pytest.approx([0.0, 0.0, 0.0])
    # ...and shrinks
    s.readings.resize(1)
    assert len(s.readings) == 1


def test_resize_on_scalar_container(go: ModuleType) -> None:
    # scalars get resize too (value-initialized to 0.0) — reserve/resize are not
    # gated to class elements, unlike new().
    s = go.Series()
    s.points.resize(2)
    assert list(s.points) == pytest.approx([0.0, 0.0])


def test_reserve_keeps_element_references_valid_across_growth(go: ModuleType) -> None:
    # reserve(n) pre-grows capacity so a run of new() does not reallocate — the
    # reference from an earlier new() stays valid (writes through) across the run.
    s = go.Series()
    s.readings.reserve(8)
    first = s.readings.new()
    for _ in range(7):  # stays within reserved capacity: no reallocation
        s.readings.new()
    first.value = 42.0  # still aliases readings[0] — no realloc happened
    assert s.readings[0].value == pytest.approx(42.0)
    assert len(s.readings) == 8


def test_generated_array_aliases_exist(go: ModuleType) -> None:
    # std::array members are opened opaque too, named element-then-extent with an `x`
    # separator so two arrays of the same element but different N never collide.
    assert hasattr(go, "ArrayDoublex3")  # Series.origin -> std::array<double, 3>
    assert hasattr(go, "ArrayShortIntx289")  # Tile.outer  -> std::array<int16, 289>
    assert hasattr(go, "ArrayShortIntx256")  # Tile.inner  -> std::array<int16, 256>
    assert hasattr(go, "ArrayFloatx4")  # inherited Corners.corners (non-welded base)


def test_array_member_is_opaque_and_writes_through(go: ModuleType) -> None:
    s = go.Series()
    assert isinstance(s.origin, go.ArrayDoublex3)
    assert not isinstance(s.origin, list)
    assert len(s.origin) == 3  # fixed length
    s.origin[0] = 10.0
    s.origin[2] = 30.0
    assert go.origin_at(s, 0) == pytest.approx(10.0)  # reached the C++ array
    assert go.origin_at(s, 2) == pytest.approx(30.0)
    assert list(s.origin) == pytest.approx([10.0, 0.0, 30.0])  # iteration


def test_array_negative_index_and_bounds(go: ModuleType) -> None:
    s = go.Series()
    s.origin[-1] = 9.0  # negative index wraps
    assert go.origin_at(s, 2) == pytest.approx(9.0)
    with pytest.raises(IndexError):
        _ = s.origin[3]
    with pytest.raises(IndexError):
        s.origin[3] = 1.0


def test_array_whole_assignment_from_sequence(go: ModuleType) -> None:
    # The working idiom must survive opaqueness: rebinding the attribute from a
    # length-N sequence still works (an implicit conversion behind def_rw).
    s = go.Series()
    s.origin = [1.0, 2.0, 3.0]
    assert go.origin_at(s, 1) == pytest.approx(2.0)
    s.origin = (4.0, 5.0, 6.0)  # any iterable of the right length
    assert go.origin_at(s, 2) == pytest.approx(6.0)
    # a wrong length is rejected, not silently truncated/padded
    with pytest.raises((ValueError, TypeError)):
        s.origin = [1.0, 2.0]
    with pytest.raises((ValueError, TypeError)):
        s.origin = [1.0, 2.0, 3.0, 4.0]


def test_array_has_no_size_changing_ops(go: ModuleType) -> None:
    s = go.Series()
    assert not hasattr(s.origin, "append")
    assert not hasattr(s.origin, "resize")
    assert not hasattr(s.origin, "clear")


def test_array_scalar_numpy_zero_copy(go: ModuleType) -> None:
    np = pytest.importorskip("numpy")
    s = go.Series()
    s.origin = [1.0, 2.0, 3.0]
    a = np.asarray(s.origin)
    assert a.dtype == np.float64
    assert a.shape == (3,)
    assert not a.flags["OWNDATA"]  # a view, not a copy
    a[1] = 7.0  # write through the shared buffer
    assert go.origin_at(s, 1) == pytest.approx(7.0)


def test_vector_of_array_bearing_struct_writes_through(go: ModuleType) -> None:
    # The wowlib chain: an opaque std::vector whose element (Tile) has std::array
    # members. h.tiles[t].outer[i] = v must persist through the whole reference chain
    # (live vector element -> live array member -> array element).
    s = go.Series()
    s.tiles.append(go.Tile())
    assert isinstance(s.tiles[0].outer, go.ArrayShortIntx289)
    assert len(s.tiles[0].outer) == 289
    s.tiles[0].outer[0] = 1234
    s.tiles[0].outer[288] = -5
    assert go.tile_outer_at(s, 0, 0) == 1234  # reached C++ storage
    assert go.tile_outer_at(s, 0, 288) == -5


def test_array_of_int16_is_zero_copy_numpy(go: ModuleType) -> None:
    np = pytest.importorskip("numpy")
    s = go.Series()
    s.tiles.append(go.Tile())
    a = np.asarray(s.tiles[0].outer)
    assert a.dtype == np.int16
    assert a.shape == (289,)
    assert not a.flags["OWNDATA"]  # zero-copy int16 view
    a[0] = 1234
    assert go.tile_outer_at(s, 0, 0) == 1234


def test_inherited_trait_base_array_is_opaque(go: ModuleType) -> None:
    # Corners is NOT welded; its std::array member is flattened into Patch, and the
    # generator collected it there — so Patch.corners binds opaque too.
    p = go.Patch()
    assert isinstance(p.corners, go.ArrayFloatx4)
    assert len(p.corners) == 4
    p.corners = [1.0, 2.0, 3.0, 4.0]
    assert list(p.corners) == pytest.approx([1.0, 2.0, 3.0, 4.0])


def test_pod_element_gets_numpy_structured_view(go: ModuleType) -> None:
    # Reading { double value; } is a POD struct, so its opaque vector also exposes a
    # numpy-free structured array view (via __array_interface__).
    np = pytest.importorskip("numpy")
    s = go.Series()
    s.readings.append(go.Reading(1.5))
    s.readings.append(go.Reading(2.5))
    a = np.asarray(s.readings)
    assert a.dtype.names == ("value",)
    assert not a.flags["OWNDATA"]  # zero-copy
    a[0]["value"] = 9.0  # write through
    assert s.readings[0].value == pytest.approx(9.0)
