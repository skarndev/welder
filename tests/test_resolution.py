"""End-to-end tests of welder's compile-time member resolution, observed through
the generated pybind11 module: a member is bound iff it appears as an attribute.
"""

from __future__ import annotations

from types import ModuleType

import pytest

from conftest import public_attrs

# Each case: (struct, member, should_be_bound). The struct names a type defined
# in tests/cpp/bindings.cpp; `bound` is whether welder should expose `member` to
# Python given that struct's policy and the member's marks. ids describe intent.
MEMBER_CASES = [
    # --- automatic policy: bind everything unless excluded -------------------
    pytest.param("Automatic", "kept", True, id="automatic/plain-member-is-bound"),
    pytest.param("Automatic", "excl_all", False, id="automatic/exclude-all-languages"),
    pytest.param("Automatic", "excl_py", False, id="automatic/exclude-python"),
    pytest.param("Automatic", "excl_lua", True, id="automatic/exclude-other-language-kept"),
    pytest.param("Automatic", "incl_py", True, id="automatic/redundant-include-kept"),
    # --- opt_in policy: bind only what is explicitly included ----------------
    pytest.param("OptIn", "unmarked", False, id="opt_in/unmarked-not-bound"),
    pytest.param("OptIn", "incl_all", True, id="opt_in/include-all-languages"),
    pytest.param("OptIn", "incl_py", True, id="opt_in/include-python"),
    pytest.param("OptIn", "incl_lua", False, id="opt_in/include-other-language-not-bound"),
    pytest.param("OptIn", "incl_then_excl", False, id="opt_in/exclude-beats-include"),
    # --- methods resolve the same way as data members ------------------------
    pytest.param("Counter", "increment", True, id="method/bound"),
    pytest.param("Counter", "value", True, id="method/const-bound"),
    pytest.param("Counter", "version", True, id="static-method/bound"),
    pytest.param("Counter", "secret", False, id="method/excluded"),
]


@pytest.mark.parametrize(("struct", "member", "bound"), MEMBER_CASES)
def test_member_binding(
    mod: ModuleType, struct: str, member: str, bound: bool
) -> None:
    instance = getattr(mod, struct)()
    assert (member in public_attrs(instance)) is bound


def test_bound_members_are_exactly_the_expected_set(mod: ModuleType) -> None:
    assert public_attrs(mod.Values()) == {"i", "d", "s"}


@pytest.mark.parametrize(
    ("field", "value"),
    [
        pytest.param("i", 7, id="int-field"),
        pytest.param("d", 2.5, id="double-field"),
        pytest.param("s", "hi", id="string-field"),
    ],
)
def test_readwrite_roundtrip(mod: ModuleType, field: str, value: object) -> None:
    instance = mod.Values()
    setattr(instance, field, value)
    assert getattr(instance, field) == value
