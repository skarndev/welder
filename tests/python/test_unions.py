"""Tests for welder's union stance.

Unions never bind: C++ cannot observe which union member is active, so any
generated accessor could read an inactive member (undefined behavior). Welder
makes every attempt a designed hard compile error (locked by the
``negcompile.union_*`` cases); these specs assert the other side — the escape
hatches keep enclosing types weldable, and ``std::variant`` is the blessed
path, crossing as a plain value.
C++ side: tests/common/cpp/unions.hpp.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def unions(mod: ModuleType) -> ModuleType:
    # The cases bind under the `unions` submodule (C++ side: namespace `unions`).
    return cast(ModuleType, mod.unions)


# --- a plain union in a swept namespace is skipped ----------------------------
def test_a_plain_union_in_the_namespace_does_not_bind(unions: ModuleType) -> None:
    assert not hasattr(unions, "Payload")


# --- named union-typed member: mark::exclude + safe accessor ------------------
def test_excluded_union_member_is_absent(unions: ModuleType) -> None:
    p = unions.Packet()
    assert p.kind == 0
    assert "payload" not in public_attrs(p)


def test_safe_accessor_reads_the_active_member(unions: ModuleType) -> None:
    # `code` reads the member C++ knows is active (brace-init of a union
    # initializes its first member) — the accessor-function remedy.
    assert unions.Packet().code() == 7


# --- anonymous union member: structurally unbindable, skipped -----------------
def test_anonymous_union_members_are_skipped(unions: ModuleType) -> None:
    f = unions.Frame()
    assert (f.tag, f.checksum) == (1, 2)
    assert {"raw", "scaled"}.isdisjoint(public_attrs(f))


def test_unnamed_field_disables_the_aggregate_constructor(
    unions: ModuleType,
) -> None:
    # The synthesized field constructor would leak the anonymous union as a
    # positional parameter, so it is not synthesized; only the default ctor.
    with pytest.raises(TypeError):
        unions.Frame(1, 2)


# --- the blessed path: std::variant crosses as a value ------------------------
def test_variant_member_defaults_to_the_first_alternative(
    unions: ModuleType,
) -> None:
    assert unions.Holder().value == 0


def test_variant_member_roundtrips_both_alternatives(unions: ModuleType) -> None:
    h = unions.Holder()
    h.value = 5
    assert h.value == 5
    h.value = unions.Boxed(9)
    assert h.value.n == 9


def test_variant_signature_converts_both_ways(unions: ModuleType) -> None:
    assert unions.box_if(False, 3) == 3
    assert unions.box_if(True, 3).n == 3