"""Tests for overriding C++ virtual methods from Python via a welder trampoline.

welder cannot synthesize the trampoline subclass (the vtable forces each override
to be a hand-written member function), but it drives everything around it from
reflection: the slot count, the class-level ``class_<T, Trampoline>`` wiring, and a
compile-time check that the trampoline covers every overridable virtual. These specs
observe the *runtime* result — a Python override reached from C++.

The case binds under the ``overridable`` submodule. A backend that has not adopted
the trampoline case simply lacks the submodule, so the whole module is skipped there.
"""

# These are behavioral runtime tests: they subclass a class reached through the
# ModuleType fixture (an ``Any``), so strict mypy's subclassing-Any checks do not
# apply — relax just those, as the config note about runtime specs anticipates.
# mypy: disable-error-code="misc, name-defined"

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def ov(mod: ModuleType) -> ModuleType:
    sub = getattr(mod, "overridable", None)
    if sub is None:
        pytest.skip("backend has no `overridable` submodule (trampolines) yet")
    return cast(ModuleType, sub)


# --- the C++ default (no Python override) -----------------------------------
def test_base_uses_cpp_defaults(ov: ModuleType) -> None:
    a = ov.Animal()
    assert a.speak() == "..."
    assert a.legs() == 4
    # describe() is a C++ method calling the virtuals; with no override it sees the
    # C++ implementations.
    assert a.describe() == "... on 4 legs"


# --- a Python subclass overriding a virtual ---------------------------------
def test_python_override_is_reached_from_cpp(ov: ModuleType) -> None:
    class Dog(ov.Animal):
        def speak(self) -> str:
            return "woof"

    d = Dog()
    # direct call hits the Python override...
    assert d.speak() == "woof"
    # ...and, crucially, the C++ describe() dispatches *back into* the override.
    assert d.describe() == "woof on 4 legs"


def test_partial_override_mixes_python_and_cpp(ov: ModuleType) -> None:
    class Millipede(ov.Animal):
        def legs(self) -> int:
            return 1000

    m = Millipede()
    # speak() falls back to the C++ default; legs() is the Python override.
    assert m.describe() == "... on 1000 legs"


def test_override_from_cpp_via_a_new_instance(ov: ModuleType) -> None:
    # A fresh subclass instance created and immediately described through C++.
    class Snake(ov.Animal):
        def speak(self) -> str:
            return "hiss"

        def legs(self) -> int:
            return 0

    assert Snake().describe() == "hiss on 0 legs"


# --- a pure virtual on an abstract base -------------------------------------
def test_pure_virtual_override_is_reached_from_cpp(ov: ModuleType) -> None:
    # Shape.area() is pure virtual; a Python subclass must supply it, and C++'s
    # scaled_area() (which calls area()) then dispatches into the override.
    class Circle(ov.Shape):
        def __init__(self, r: float) -> None:
            super().__init__()
            self.r = r

        def area(self) -> float:
            return 3.14159 * self.r * self.r

    c = Circle(2.0)
    assert round(c.area(), 2) == 12.57
    assert round(c.scaled_area(10.0), 1) == 125.7  # C++ -> Python pure-virtual call


def test_abstract_base_is_constructible_but_pure_virtual_raises(ov: ModuleType) -> None:
    # welder registers the concrete trampoline's constructor (so subclasses work), so
    # the abstract base itself constructs — but calling the unoverridden pure virtual
    # raises rather than returning a bogus value.
    s = ov.Shape()
    with pytest.raises(RuntimeError):
        s.area()
    with pytest.raises(RuntimeError):
        s.scaled_area(2.0)  # C++ reaches the un-provided pure virtual


def test_pure_virtual_not_overridden_in_subclass_raises(ov: ModuleType) -> None:
    class NotAShape(ov.Shape):
        pass  # forgot to override area()

    with pytest.raises(RuntimeError):
        NotAShape().area()


# --- a derived class overriding an INHERITED virtual ------------------------
def test_derived_class_overrides_inherited_virtual(ov: ModuleType) -> None:
    # Bird inherits Animal.speak()/legs() without re-declaring them. A Python subclass
    # of Bird overrides the *inherited* speak(); the C++ describe() (defined on Animal,
    # calling speak() polymorphically) must dispatch into the override — proving Bird's
    # trampoline covers inherited virtuals, not just Bird's own fly().
    class Sparrow(ov.Bird):
        def speak(self) -> str:
            return "tweet"

    s = Sparrow()
    assert s.speak() == "tweet"
    assert s.fly() == "flap"  # inherited-through-trampoline C++ default
    assert s.describe() == "tweet on 4 legs"  # C++ -> Python, via the inherited virtual


def test_derived_class_overrides_own_virtual(ov: ModuleType) -> None:
    class Penguin(ov.Bird):
        def fly(self) -> str:
            return "cannot fly"

        def legs(self) -> int:  # inherited virtual
            return 2

    p = Penguin()
    assert p.fly() == "cannot fly"
    assert p.describe() == "... on 2 legs"  # inherited speak default + overridden legs


# --- a per-method bind_flat virtual -----------------------------------------
def test_bind_flat_virtual_is_bound_but_not_overridable(ov: ModuleType) -> None:
    # kingdom() is virtual but marked [[=welder::rods::python::bind_flat]]: it is
    # still a callable bound method returning the C++ value...
    assert ov.Animal().kingdom() == "Animalia"

    # ...and it has no trampoline slot, so a C++ path would not dispatch to a Python
    # override. (There is no C++ method here that calls kingdom(), which is the point:
    # it is deliberately excluded from override routing.)
    class Fungus(ov.Animal):
        def kingdom(self) -> str:  # shadows only at the Python level
            return "Fungi"

    # The Python-level attribute still resolves to the subclass method...
    assert Fungus().kingdom() == "Fungi"