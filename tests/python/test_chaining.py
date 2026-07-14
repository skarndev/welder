"""The returned-handle mixing story: hand-written pybind11/nanobind registrations
chain onto the handles welder's entry points return (C++ side: chaining.hpp —
`weld_type` returns the rod's class handle, `weld_function` the bound function
object).
"""

from __future__ import annotations

from types import ModuleType


def test_hand_registered_method_on_weld_type_handle(mod: ModuleType) -> None:
    g = mod.chaining.Gadget()
    g.n = 21
    assert g.doubled() == 42


def test_weld_function_returns_the_bound_function_object(mod: ModuleType) -> None:
    assert mod.chaining.twice(3) == 6
    # The alias was assigned from the returned handle, so it IS the same object.
    assert mod.chaining.twice_alias is mod.chaining.twice
    assert mod.chaining.twice_alias(4) == 8