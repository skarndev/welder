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

# --- template instantiations joining welder-bound overload sets -----------------
def test_member_template_instantiation_joins_the_overload_set(mod: ModuleType) -> None:
    # Mixer has non-template mix(int)/mix(str) — bound by welder as one overload
    # group (the member TEMPLATE is not a function; the walk skips it) — plus a
    # chained mix<double> def'd on the returned class handle under the same name.
    m = mod.chaining.Mixer()
    assert m.mix(3) == "int:3"          # welder-bound
    assert m.mix("hi") == "str:hi"      # welder-bound
    assert m.mix(2.5).startswith("tpl:2.5")  # the chained instantiation

    # One merged Python callable serves all three (not a shadowing re-def).
    doc = mod.chaining.Mixer.mix.__doc__ or ""
    assert doc.count("mix(") >= 3


def test_free_function_template_instantiation_joins_the_overload_set(
    mod: ModuleType,
) -> None:
    # blend(int)/blend(str) bind as a swept group; blend<double> is appended with
    # the framework's own module def (weld_function cannot name `blend` here —
    # an overload set — and substitute() cannot form blend<double> either, since
    # the template shares its name with non-template overloads).
    ch = mod.chaining
    assert ch.blend(3) == "int:3"
    assert ch.blend("hi") == "str:hi"
    assert ch.blend(2.5).startswith("tpl:2.5")
