"""Behavioral tests for bound constructors and methods (the resolution side —
which members are exposed — lives in test_resolution.py).
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def meth(mod: ModuleType) -> ModuleType:
    # The cases bind under the `methods` submodule (C++ side: namespace `methods`).
    return cast(ModuleType, mod.methods)


@pytest.mark.parametrize(
    ("args", "expected_value"),
    [
        pytest.param((), 0, id="default-constructor"),
        pytest.param((5,), 5, id="int-constructor"),
    ],
)
def test_constructor(meth: ModuleType, args: tuple[int, ...], expected_value: int) -> None:
    assert meth.Counter(*args).value() == expected_value


def test_method_mutation(meth: ModuleType) -> None:
    c = meth.Counter(0)
    c.increment()
    c.increment()
    c.add(10)
    assert c.value() == 12


def test_static_method(meth: ModuleType) -> None:
    assert meth.Counter.version() == 7


@pytest.mark.parametrize(
    ("args", "expected"),
    [
        pytest.param((5,), 15, id="one-arg-overload"),
        pytest.param((5, 5), 20, id="two-arg-overload"),
    ],
)
def test_overloaded_method_dispatch(
    meth: ModuleType, args: tuple[int, ...], expected: int
) -> None:
    assert meth.Calc(10).sum(*args) == expected


# --- argument names ---------------------------------------------------------
def test_method_argument_is_named(meth: ModuleType) -> None:
    # The C++ parameter name reaches Python (a keyword arg), not arg0.
    assert meth.Counter(0).add(n=3) is None
    # The signature carries the name `n:` (backends spell the type differently —
    # e.g. pybind11 3.x renders the convertible arg as `typing.SupportsInt`).
    assert "n:" in meth.Counter.add.__doc__
    assert "arg0" not in meth.Counter.add.__doc__


def test_constructor_argument_is_named(meth: ModuleType) -> None:
    assert meth.Counter(start=9).value() == 9


def test_free_function_arguments_are_named(mod: ModuleType) -> None:
    # A free function borrowed from the `documented` submodule: its C++ parameter
    # names reach Python as keyword arguments.
    assert mod.documented.add(a=2, b=3) == 5


# --- aggregate initialization -----------------------------------------------
def test_aggregate_field_constructor(meth: ModuleType) -> None:
    v = meth.Vec2(1.5, 2.5)
    assert (v.x, v.y) == (1.5, 2.5)


def test_aggregate_keyword_constructor(meth: ModuleType) -> None:
    v = meth.Vec2(x=1.0, y=2.0)
    assert (v.x, v.y) == (1.0, 2.0)


def test_aggregate_default_constructor_still_bound(meth: ModuleType) -> None:
    v = meth.Vec2()
    assert (v.x, v.y) == (0.0, 0.0)


# --- NSDMI defaults on the synthesized field constructor ---------------------
def test_aggregate_nsdmi_suffix_defaults(meth: ModuleType) -> None:
    # Only the required prefix is passed; the NSDMI suffix fills in.
    w = meth.Window(4, "editor")
    assert (w.samples, w.title) == (4, "editor")
    assert (w.width, w.height, w.resizable) == (800, 600, True)


def test_aggregate_keyword_skips_middle_default(meth: ModuleType) -> None:
    # A keyword argument can skip past earlier defaulted fields.
    w = meth.Window(4, "editor", height=900)
    assert (w.width, w.height) == (800, 900)


def test_aggregate_nsdmi_before_required_stays_required(meth: ModuleType) -> None:
    # `samples` has an NSDMI but precedes the required `title`: no gaps in a
    # parameter list, so it must still be passed.
    with pytest.raises(TypeError):
        meth.Window(title="editor")


def test_aggregate_all_nsdmi_fields_default(meth: ModuleType) -> None:
    # Vec2's fields are all NSDMI'd, so every one is omissible now.
    v = meth.Vec2(x=3.0)
    assert (v.x, v.y) == (3.0, 0.0)


def test_const_member_aggregate(meth: ModuleType) -> None:
    # Const members keep the struct an aggregate: the field constructor works,
    # the NSDMI default applies, and the fields are read-only.
    f = meth.Frozen("locked")
    assert (f.name, f.level) == ("locked", 1)
    assert meth.Frozen("up", 3).level == 3
    with pytest.raises(AttributeError):
        f.level = 9


def test_no_reassign_member(meth: ModuleType) -> None:
    # `pinned` is a mutable `int` in C++, forced read-only by mark::no_reassign:
    # reading works, rebinding raises, and the unmarked `writable` control still sets.
    a = meth.Anchored()
    assert (a.pinned, a.writable) == (7, 0)
    a.writable = 5
    assert a.writable == 5
    with pytest.raises(AttributeError):
        a.pinned = 99
    assert a.pinned == 7


# --- welded-class NSDMI defaults bind lazily (shutdown-leak regression) ------
# A welded-class default is NOT stored as a Python object in the function
# record (nanobind cannot GC-traverse bound instances, so nested default
# chains leak at shutdown). The parameter binds `Optional[F] = None` and the
# C++ side materializes the NSDMI value.
def test_lazy_default_omission_materializes_nsdmi(meth: ModuleType) -> None:
    p = meth.Plot("scatter")
    assert p.title == "scatter"
    assert (p.region.span.lo.x, p.region.span.hi.x) == (0.0, 0.0)
    assert p.region.pad == 1.5


def test_lazy_default_explicit_none_means_default(meth: ModuleType) -> None:
    p = meth.Plot("scatter", None)
    assert (p.region.span.lo.x, p.region.pad) == (0.0, 1.5)


def test_lazy_default_explicit_value_applies(meth: ModuleType) -> None:
    region = meth.Region(meth.Span(meth.Corner(1.0), meth.Corner(2.0)), 3.0)
    p = meth.Plot("scatter", region)
    assert (p.region.span.lo.x, p.region.span.hi.x) == (1.0, 2.0)
    assert p.region.pad == 3.0


def test_lazy_default_keyword_skips_earlier_lazy(meth: ModuleType) -> None:
    # `span` (lazy) is skipped by keyword; the native `pad` default after a
    # lazy field still binds as a real Python default.
    r = meth.Region(pad=9.0)
    assert (r.span.lo.x, r.pad) == (0.0, 9.0)
    s = meth.Span(hi=meth.Corner(5.0))
    assert (s.lo.x, s.hi.x) == (0.0, 5.0)


def test_lazy_default_shutdown_reports_no_leaks(mod: ModuleType) -> None:
    # Import the module in a fresh interpreter and let it exit: a rod that
    # stores bound-class defaults in function records makes nanobind print
    # "nanobind: leaked N types!" here. Trivially green under pybind11 (which
    # prints no such report) — the semantics tests above cover both backends.
    import os
    import pathlib
    import subprocess
    import sys

    assert mod.__file__ is not None
    env = dict(os.environ)
    moddir = str(pathlib.Path(mod.__file__).parent)
    env["PYTHONPATH"] = moddir + os.pathsep + env.get("PYTHONPATH", "")
    proc = subprocess.run(
        [sys.executable, "-c", f"import {mod.__name__}"],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    assert proc.returncode == 0, proc.stderr
    assert "leaked" not in proc.stderr, proc.stderr
