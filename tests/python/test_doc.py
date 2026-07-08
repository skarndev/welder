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


def test_method_return_docstring(doc: ModuleType) -> None:
    text = doc.Circle.area.__doc__
    assert "Returns:" in text
    assert "    the area" in text


def test_static_method_docstring(doc: ModuleType) -> None:
    assert "The unit circle." in doc.Circle.unit.__doc__
    assert doc.Circle.unit().r == 1.0


def test_undocumented_method_has_no_welder_doc(doc: ModuleType) -> None:
    # Still bound and callable; just no welder-contributed text beyond pybind's
    # auto-generated signature.
    assert doc.Circle(2.0).circumference() == pytest.approx(2 * 3.14159 * 2.0)
    assert "Args:" not in (doc.Circle.circumference.__doc__ or "")


# --- field docstrings --------------------------------------------------------
def test_field_docstring(doc: ModuleType) -> None:
    # A data member is a pybind11 property; its welder doc is the property __doc__
    # verbatim (no signature line, unlike methods).
    assert doc.Circle.r.__doc__ == "The radius."


def test_mutable_field_is_readwrite_with_doc(doc: ModuleType) -> None:
    m = doc.Marker()
    assert doc.Marker.note.__doc__ == "A mutable note."
    m.note = "hi"  # read/write
    assert m.note == "hi"


def test_const_field_is_readonly_with_doc(doc: ModuleType) -> None:
    m = doc.Marker()
    assert doc.Marker.id.__doc__ == "The immutable id."
    assert m.id == 7
    with pytest.raises(AttributeError):
        m.id = 9  # const member -> read-only property


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


# --- return value docstrings ------------------------------------------------
def test_function_docstring_with_returns_block(doc: ModuleType) -> None:
    text = doc.add.__doc__
    # summary, Args, and Returns coexist (and in that order).
    assert "Add two integers." in text
    assert text.index("Args:") < text.index("Returns:")
    assert "    their sum" in text


def test_returns_only_function_has_just_a_returns_block(doc: ModuleType) -> None:
    text = doc.twice.__doc__
    assert "Returns:" in text
    assert "    the doubled value" in text
    assert "Args:" not in text
    assert doc.twice(21) == 42


# --- multiline / raw-string + dedent -----------------------------------------
def test_class_multiline_docstring_is_dedented(doc: ModuleType) -> None:
    # The C++ doc is indented to match the source; welder dedents it (PEP 257) so
    # the common indentation is gone, blank/trailing lines are trimmed, and the
    # example block keeps its *relative* extra indentation.
    assert doc.Gadget.__doc__ == (
        "A gadget.\n"
        "\n"
        "Example:\n"
        "    >>> Gadget().tag\n"
        "    0"
    )


def test_function_multiline_summary_param_and_return_docs(doc: ModuleType) -> None:
    text = doc.combine.__doc__
    # multiline summary, dedented (leading source indentation stripped)
    assert "Combine two values.\n\nDetailed multiline\ndescription." in text
    # a multiline parameter doc: continuation lines indented under the Args block
    assert "    a: the first operand,\n        spanning two lines" in text
    assert "    b: the second operand" in text
    # a multiline return doc: continuation lines indented under Returns
    assert "Returns:\n    the combined result,\n    described over two lines" in text
    assert doc.combine(2, 3) == 5


# --- alternative docstring styles (numpy / sphinx) --------------------------
# `add` (summary + two param docs + a return doc) is re-welded through a
# numpy-styled and a sphinx-styled rod into sibling submodules, so the *same*
# function renders in each Python docstring dialect. The style is the rod's
# DocStyle template parameter; the driver is unchanged.
def test_numpy_style_docstring(mod: ModuleType) -> None:
    text = cast(ModuleType, mod.documented_numpy).add.__doc__
    assert "Add two integers." in text
    # underlined numpydoc sections, not Google's `Args:`/`Returns:`
    assert "Parameters\n----------" in text
    assert "Returns\n-------" in text
    assert "Args:" not in text
    # a parameter renders as `name` then its indented body (no type available)
    assert "a\n    left operand" in text


def test_sphinx_style_docstring(mod: ModuleType) -> None:
    text = cast(ModuleType, mod.documented_sphinx).add.__doc__
    assert "Add two integers." in text
    # reST field list, not Google/NumPy sections
    assert ":param a: left operand" in text
    assert ":param b: right operand" in text
    assert ":returns: their sum" in text
    assert "Args:" not in text
    assert "Parameters" not in text


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
