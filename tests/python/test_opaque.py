"""Tests for opaque, reference-semantic STL containers.

Unlike test_stl.py (containers bound BY VALUE — the framework casters copy a
``std::vector`` to a fresh ``list`` on every access), these containers are welded
opaque via a namespace-scope alias, so they bind BY REFERENCE: mutation writes
through to the C++ object, ``push_back`` shows up as ``append``, and a scalar
vector exposes its ``data()`` zero-copy to numpy. C++ side: tests/common/cpp/opaque.hpp
(``bind_vector`` for ``std::vector``, ``bind_map`` for ``std::map`` /
``std::unordered_map``). Runs against both Python rods.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def op(mod: ModuleType) -> ModuleType:
    # The cases bind under the `opaque` submodule (C++ side: namespace `opaque`).
    return cast(ModuleType, mod.opaque)


# --- the bound container is a distinct wrapper type, NOT a list ---------------
def test_opaque_vector_is_not_a_list(op: ModuleType) -> None:
    v = op.FloatVector()
    assert not isinstance(v, list)
    assert type(v).__name__ == "FloatVector"


# --- reference semantics: mutation writes through to C++ ----------------------
def test_member_mutation_writes_through(op: ModuleType) -> None:
    s = op.Signal()
    assert isinstance(s.samples, op.FloatVector)
    # append == push_back, straight onto the C++ vector
    s.samples.append(1.5)
    s.samples.append(2.5)
    assert op.sum_samples(s) == pytest.approx(4.0)
    # __setitem__ writes through too
    s.samples[0] = 10.0
    assert op.sum_samples(s) == pytest.approx(12.5)
    assert len(s.samples) == 2


def test_sequence_protocol(op: ModuleType) -> None:
    v = op.FloatVector()
    for x in (1.0, 2.0, 3.0, 4.0):
        v.append(x)
    assert len(v) == 4
    assert v[0] == pytest.approx(1.0)
    assert list(v[1:3]) == pytest.approx([2.0, 3.0])  # slicing
    assert [x for x in v] == pytest.approx([1.0, 2.0, 3.0, 4.0])  # iteration


# --- numpy zero-copy over the raw buffer (scalar element) --------------------
def test_numpy_zero_copy(op: ModuleType) -> None:
    np = pytest.importorskip("numpy")
    s = op.Signal()
    for x in (10.0, 20.0, 30.0):
        s.samples.append(x)
    a = np.asarray(s.samples)
    assert a.dtype == np.float32
    assert not a.flags["OWNDATA"]  # a view, not a copy
    a[0] = 7.0  # mutate through the shared buffer
    assert op.sum_samples(s) == pytest.approx(57.0)
    assert s.samples[0] == pytest.approx(7.0)


# --- bind_vector with a welded CLASS element ---------------------------------
def test_vector_of_welded_class(op: ModuleType) -> None:
    pts = op.PointList()
    pts.append(op.Point2D(3.0, 4.0))
    pts.append(op.Point2D(5.0, 6.0))
    assert len(pts) == 2
    assert isinstance(pts[0], op.Point2D)
    assert pts[0].x == pytest.approx(3.0)
    assert pts[1].y == pytest.approx(6.0)
    # a class element has no scalar buffer protocol (memoryview needs one)
    with pytest.raises((TypeError, BufferError)):
        memoryview(pts)


def test_vector_element_access_is_a_live_reference(op: ModuleType) -> None:
    # __getitem__ / __iter__ over a welded-class element hand out a LIVE reference
    # aliasing the C++ element, not a snapshot copy — so in-place mutation persists.
    pts = op.PointList()
    pts.append(op.Point2D(3.0, 4.0))
    pts.append(op.Point2D(5.0, 6.0))

    # mutate through __getitem__; a fresh read sees the write (a copy would drop it)
    pts[0].x = 30.0
    assert pts[0].x == pytest.approx(30.0)
    # and C++ sees it too — the write reached the container's storage
    assert op.point_x_at(pts, 0) == pytest.approx(30.0)

    # mutate through iteration as well
    for p in pts:
        p.y = 99.0
    assert [p.y for p in pts] == pytest.approx([99.0, 99.0])
    assert op.point_x_at(pts, 1) == pytest.approx(5.0)  # x untouched


def test_scalar_vector_element_is_a_copy(op: ModuleType) -> None:
    # A scalar element is returned by value (a copy) — there is nothing to alias,
    # and rebinding via __setitem__ is the write path (exercised elsewhere).
    v = op.FloatVector()
    v.append(1.0)
    x = v[0]
    assert x == pytest.approx(1.0)
    assert isinstance(x, float)


# --- numpy structured view of a POD-struct-element vector (numpy-free) --------
def test_pod_struct_numpy_structured_view(op: ModuleType) -> None:
    np = pytest.importorskip("numpy")
    pts = op.PointList()  # Point2D { double x, y; } — a POD struct
    pts.append(op.Point2D(1.0, 2.0))
    pts.append(op.Point2D(3.0, 4.0))
    a = np.asarray(pts)  # reads __array_interface__ — no numpy at build/import
    assert a.dtype.names == ("x", "y")  # reflected structured dtype
    assert not a.flags["OWNDATA"]  # zero-copy view
    assert a[0]["x"] == pytest.approx(1.0) and a[1]["y"] == pytest.approx(4.0)
    a[1]["x"] = 99.0  # writes through the shared buffer
    assert pts[1].x == pytest.approx(99.0)


# --- no_reassign: read-only binding, in-place mutation still writes through ----
def test_no_reassign_member(op: ModuleType) -> None:
    s = op.Signal()
    # The read-only binding still hands out a live reference: append/__setitem__
    # write straight through to the C++ vector.
    s.locked.append(3.0)
    s.locked.append(4.0)
    assert op.sum_locked(s) == pytest.approx(7.0)
    s.locked[0] = 10.0
    assert op.sum_locked(s) == pytest.approx(14.0)
    assert len(s.locked) == 2
    # But rebinding the whole attribute is rejected (no setter descriptor).
    with pytest.raises(AttributeError):
        s.locked = op.FloatVector()
    # The control member without the mark stays reassignable.
    s.samples = op.FloatVector()  # no raise


# --- bind_map: std::map / std::unordered_map ---------------------------------
def test_ordered_map(op: ModuleType) -> None:
    m = op.IntStrMap()
    m[1] = "one"
    m[2] = "two"
    assert m[1] == "one"
    assert len(m) == 2
    assert 1 in m and 3 not in m
    assert sorted(m) == [1, 2]  # iterating a map yields its keys


def test_unordered_map(op: ModuleType) -> None:
    h = op.StrIntHash()
    h["a"] = 1
    h["b"] = 2
    assert h["a"] == 1
    assert len(h) == 2
    assert "a" in h and "z" not in h


def test_map_value_access_is_a_live_reference(op: ModuleType) -> None:
    # bind_map's __getitem__ over a welded-class value hands out a LIVE reference,
    # so `m[k].x = v` writes through to the C++ mapped object (a copy would drop it).
    m = op.PointMap()
    m[1] = op.Point2D(3.0, 4.0)
    m[1].x = 30.0
    assert m[1].x == pytest.approx(30.0)
    assert op.mapped_point_x(m, 1) == pytest.approx(30.0)  # reached C++ storage
