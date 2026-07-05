"""End-to-end tests of welder's compile-time member resolution, observed through
the generated pybind11 module: a member is bound iff it appears as an attribute.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def res(mod: ModuleType) -> ModuleType:
    # The cases bind under the `resolution` submodule (C++ side: namespace `resolution`).
    return cast(ModuleType, mod.resolution)


# Each case: (submodule, struct, member, should_be_bound). The struct names a type
# bound under that submodule; `bound` is whether welder should expose `member` to
# Python given that struct's policy and the member's marks. The Counter cases live
# in the `methods` submodule (methods resolve the same way as data members); the
# rest live in `resolution`. ids describe intent.
MEMBER_CASES = [
    # --- automatic policy: bind everything unless excluded -------------------
    pytest.param("resolution", "Automatic", "kept", True, id="automatic/plain-member-is-bound"),
    pytest.param("resolution", "Automatic", "excl_all", False, id="automatic/exclude-all-languages"),
    pytest.param("resolution", "Automatic", "excl_py", False, id="automatic/exclude-python"),
    pytest.param("resolution", "Automatic", "excl_lua", True, id="automatic/exclude-other-language-kept"),
    pytest.param("resolution", "Automatic", "incl_py", True, id="automatic/redundant-include-kept"),
    # --- opt_in policy: bind only what is explicitly included ----------------
    pytest.param("resolution", "OptIn", "unmarked", False, id="opt_in/unmarked-not-bound"),
    pytest.param("resolution", "OptIn", "incl_all", True, id="opt_in/include-all-languages"),
    pytest.param("resolution", "OptIn", "incl_py", True, id="opt_in/include-python"),
    pytest.param("resolution", "OptIn", "incl_lua", False, id="opt_in/include-other-language-not-bound"),
    pytest.param("resolution", "OptIn", "incl_then_excl", False, id="opt_in/exclude-beats-include"),
    # --- methods resolve the same way as data members ------------------------
    pytest.param("methods", "Counter", "increment", True, id="method/bound"),
    pytest.param("methods", "Counter", "value", True, id="method/const-bound"),
    pytest.param("methods", "Counter", "version", True, id="static-method/bound"),
    pytest.param("methods", "Counter", "secret", False, id="method/excluded"),
    # --- access control: only public members are bound -----------------------
    pytest.param("resolution", "Access", "visible", True, id="access/public-data-bound"),
    pytest.param("resolution", "Access", "read_hidden", True, id="access/public-method-bound"),
    pytest.param("resolution", "Access", "hidden", False, id="access/private-data-not-bound"),
    pytest.param("resolution", "Access", "guarded", False, id="access/protected-data-not-bound"),
    pytest.param("resolution", "Access", "helper", False, id="access/private-method-not-bound"),
]


@pytest.mark.parametrize(("submod", "struct", "member", "bound"), MEMBER_CASES)
def test_member_binding(
    mod: ModuleType, submod: str, struct: str, member: str, bound: bool
) -> None:
    instance = getattr(getattr(mod, submod), struct)()
    assert (member in public_attrs(instance)) is bound


def test_bound_members_are_exactly_the_expected_set(res: ModuleType) -> None:
    assert public_attrs(res.Values()) == {"i", "d", "s"}


@pytest.mark.parametrize(
    ("field", "value"),
    [
        pytest.param("i", 7, id="int-field"),
        pytest.param("d", 2.5, id="double-field"),
        pytest.param("s", "hi", id="string-field"),
    ],
)
def test_readwrite_roundtrip(res: ModuleType, field: str, value: object) -> None:
    instance = res.Values()
    setattr(instance, field, value)
    assert getattr(instance, field) == value
