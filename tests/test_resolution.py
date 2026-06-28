"""End-to-end tests of welder's compile-time member resolution, observed through
the generated pybind11 module: a member is bound iff it appears as an attribute.
"""

from __future__ import annotations

from types import ModuleType

from conftest import public_attrs


class TestAutomaticPolicy:
    """Default policy: every member is bound unless excluded."""

    def test_plain_member_is_bound(self, mod: ModuleType) -> None:
        assert "kept" in public_attrs(mod.Automatic())

    def test_exclude_all_languages(self, mod: ModuleType) -> None:
        assert "excl_all" not in public_attrs(mod.Automatic())

    def test_exclude_python(self, mod: ModuleType) -> None:
        assert "excl_py" not in public_attrs(mod.Automatic())

    def test_exclude_other_language_is_kept(self, mod: ModuleType) -> None:
        # Excluded for lua only -> still bound for python.
        assert "excl_lua" in public_attrs(mod.Automatic())

    def test_redundant_include_is_kept(self, mod: ModuleType) -> None:
        # include under automatic policy is redundant, member stays bound.
        assert "incl_py" in public_attrs(mod.Automatic())


class TestOptInPolicy:
    """opt_in policy: only members explicitly included are bound."""

    def test_unmarked_member_not_bound(self, mod: ModuleType) -> None:
        assert "unmarked" not in public_attrs(mod.OptIn())

    def test_include_all_languages(self, mod: ModuleType) -> None:
        assert "incl_all" in public_attrs(mod.OptIn())

    def test_include_python(self, mod: ModuleType) -> None:
        assert "incl_py" in public_attrs(mod.OptIn())

    def test_include_other_language_not_bound(self, mod: ModuleType) -> None:
        # Included for lua only -> not bound for python.
        assert "incl_lua" not in public_attrs(mod.OptIn())

    def test_exclude_beats_include(self, mod: ModuleType) -> None:
        assert "incl_then_excl" not in public_attrs(mod.OptIn())


class TestReadWrite:
    """Bound members are read/write properties of the right type."""

    def test_full_attribute_set(self, mod: ModuleType) -> None:
        assert public_attrs(mod.Values()) == {"i", "d", "s"}

    def test_roundtrip(self, mod: ModuleType) -> None:
        v = mod.Values()
        v.i, v.d, v.s = 7, 2.5, "hi"
        assert (v.i, v.d, v.s) == (7, 2.5, "hi")
