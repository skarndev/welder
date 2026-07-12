"""Tests for GENERATED trampolines (welder::rods::trampolines).

The counterpart to test_trampoline.py: there the trampoline subclasses are hand-written;
here they are emitted at build time by welder's trampoline-generator rod (a generator
executable reflects the welded types and writes a backend-neutral header the extension
compiles in). These specs observe the runtime result — a Python override reached from
C++ — so if generation is correct they pass identically on both backends.

The case binds under the ``gen_trampolines`` submodule; a backend that has not wired
the generated header simply lacks it and the whole module is skipped there.
"""

# Behavioral runtime tests subclass classes reached through the ModuleType fixture (an
# ``Any``), so relax strict mypy's subclassing-Any checks, as in test_trampoline.py.
# mypy: disable-error-code="misc, name-defined"

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def gt(mod: ModuleType) -> ModuleType:
    sub = getattr(mod, "gen_trampolines", None)
    if sub is None:
        pytest.skip("backend has no `gen_trampolines` submodule (generated trampolines) yet")
    return cast(ModuleType, sub)


def test_generated_trampoline_reaches_python_override(gt: ModuleType) -> None:
    # No override: C++ defaults.
    assert gt.Beast().portrait() == "... on 4 limbs"

    class Cat(gt.Beast):
        def cry(self) -> str:
            return "meow"

    # The generated trampoline routes the C++ portrait() call into the Python override.
    assert Cat().portrait() == "meow on 4 limbs"


def test_generated_trampoline_covers_inherited_virtual(gt: ModuleType) -> None:
    # Raptor inherits cry()/limbs() from Beast and only adds soar(); the GENERATED
    # trampoline must still cover the inherited virtuals (overridable_virtuals folds
    # them in), so overriding the inherited cry() from Python reaches C++ portrait().
    class Eagle(gt.Raptor):
        def cry(self) -> str:
            return "screech"

    e = Eagle()
    assert e.soar() == "glide"                      # inherited-through-trampoline default
    assert e.portrait() == "screech on 4 limbs"     # C++ -> Python via inherited virtual

    class Ostrich(gt.Raptor):
        def limbs(self) -> int:
            return 2

    assert Ostrich().portrait() == "... on 2 limbs"


def test_generated_trampoline_pure_virtual(gt: ModuleType) -> None:
    class Disc(gt.Figure):
        def __init__(self, r: float) -> None:
            super().__init__()
            self.r = r

        def surface(self) -> float:
            return 3.14159 * self.r * self.r

    d = Disc(2.0)
    assert round(d.surface(), 2) == 12.57
    assert round(d.scaled(10.0), 1) == 125.7  # C++ -> Python pure-virtual call


def test_generated_bind_flat_virtual_not_routed(gt: ModuleType) -> None:
    # realm() is virtual but bind_flat, so the generator emits no override for it: it
    # stays a plain callable returning the C++ value.
    assert gt.Beast().realm() == "Animalia"