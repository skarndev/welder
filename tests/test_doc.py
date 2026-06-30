"""Tests for [[=welder::doc]] — docstrings reflected into the Python bindings.

The C++ namespace ``documented`` is bound (via ``welder::pybind11::build_module``)
under the ``documented`` submodule of the test module. welder reads each
``doc`` annotation and surfaces it: a class/namespace docstring verbatim, and a
function docstring with its parameter docs folded in Google-style. Variable docs
are intentionally dropped (module attributes have no ``__doc__``).
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def doc(mod: ModuleType) -> ModuleType:
    # mod.<attr> is Any (ModuleType.__getattr__); cast back at this boundary so
    # the dynamic access stays contained and strict mypy still covers the rest.
    return cast(ModuleType, mod.documented)


# --- class + method docstrings ----------------------------------------------
def test_class_docstring(doc: ModuleType) -> None:
    assert doc.Circle.__doc__ == "A circle."


def test_method_docstring(doc: ModuleType) -> None:
    # pybind11 prepends its own signature line; the welder doc follows it.
    assert "Compute the area." in doc.Circle.area.__doc__


def test_static_method_docstring(doc: ModuleType) -> None:
    assert "The unit circle." in doc.Circle.unit.__doc__
    assert doc.Circle.unit().r == 1.0


def test_undocumented_method_has_no_welder_doc(doc: ModuleType) -> None:
    # Still bound and callable; just no welder-contributed text beyond pybind's
    # auto-generated signature.
    assert doc.Circle(2.0).circumference() == pytest.approx(2 * 3.14159 * 2.0)
    assert "Args:" not in (doc.Circle.circumference.__doc__ or "")


# --- free function + parameter docstrings -----------------------------------
def test_function_docstring_with_google_style_args(doc: ModuleType) -> None:
    text = doc.add.__doc__
    assert "Add two integers." in text
    assert "Args:" in text
    assert "    a: left operand" in text
    assert "    b: right operand" in text
    assert doc.add(2, 3) == 5


def test_function_without_param_docs_has_no_args_block(doc: ModuleType) -> None:
    text = doc.negate.__doc__
    assert "Negate a value." in text
    assert "Args:" not in text
    assert doc.negate(5) == -5


# --- namespace docstring (adopted as the module docstring) -------------------
def test_namespace_docstring_becomes_module_doc(doc: ModuleType) -> None:
    assert doc.__doc__ == "The documented sample namespace."


# --- variable docs are ignored ----------------------------------------------
def test_variable_doc_is_ignored_but_value_still_bound(doc: ModuleType) -> None:
    assert doc.ANSWER == 42  # bound as usual; its doc annotation is simply dropped


# --- build_module hooks -----------------------------------------------------
def test_build_module_runs_pre_and_post_hooks(doc: ModuleType) -> None:
    assert doc.pre_marker == 1
    assert doc.post_marker == 2
