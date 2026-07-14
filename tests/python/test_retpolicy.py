"""Return-value policy (``[[=welder::return_policy]]``) and ``keep_alive``.

The Python rods honor the policy (pybind11 ``return_value_policy`` / nanobind
``rv_policy``); the Lua rods ignore it (ownership there is structural) — so the
divergence between ``reference_internal`` and ``copy`` is a Python-only assertion,
checked here. The reference-to-a-temporary contradiction is a *compile* error and
is covered by the negative-compile CTest, not here.
"""

from __future__ import annotations

import gc
from types import ModuleType
from typing import cast

import pytest


@pytest.fixture()
def rp(mod: ModuleType) -> ModuleType:
    # C++ side: namespace `retpolicy`, bound under a same-named submodule.
    return cast(ModuleType, mod.retpolicy)


def test_reference_internal_is_a_live_view(rp: ModuleType) -> None:
    o = rp.Owner()
    # reference_internal hands back a non-owning view aliasing the C++ member, so
    # a write through it writes the owner's object.
    o.view().v = 5
    assert o.inner_v() == 5


def test_copy_is_an_independent_snapshot(rp: ModuleType) -> None:
    o = rp.Owner()
    o.view().v = 5  # establish a known state
    # copy hands back an independent object; writing it leaves the owner untouched.
    snap = o.snapshot()
    snap.v = 99
    assert snap.v == 99
    assert o.inner_v() == 5


def test_reference_internal_keeps_the_parent_alive(rp: ModuleType) -> None:
    # The owner is a temporary; reference_internal ties its lifetime to the view,
    # so reading the view after a collection is safe (no dangling parent).
    view = rp.Owner().view()
    gc.collect()
    assert view.v == 0


def test_keep_alive_extends_the_argument(rp: ModuleType) -> None:
    r = rp.Registry()
    item = rp.Inner(42)
    r.track(item)
    del item
    gc.collect()
    # Without keep_alive(1, 2) the tracked Inner would have been freed at `del`;
    # the dependency keeps it alive for the registry's lifetime.
    assert r.first_v() == 42