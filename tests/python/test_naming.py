"""Name styling + weld_as (mirrors tests/lua/spec/naming_spec.lua).

The `styling` submodule is bound through a PEP 8 styled welder, so the camelCase
C++ names come out snake_case — except the one method carrying a per-language
`weld_as`, which is forced verbatim and bypasses the style entirely.
"""

from __future__ import annotations

from types import ModuleType
from typing import cast

import pytest

from conftest import public_attrs


@pytest.fixture()
def styling(mod: ModuleType) -> ModuleType:
    # C++ side: namespace `styling`, bound via a welder<Rod, python::pep8>.
    return cast(ModuleType, mod.styling)


def test_class_name_is_capwords(styling: ModuleType) -> None:
    # PEP 8 keeps class names CapWords; an already-PascalCase C++ name is unchanged.
    assert hasattr(styling, "HttpClient")


def test_methods_are_snake_cased(styling: ModuleType) -> None:
    c = styling.HttpClient("http://x")
    assert c.send_request() == "http://x/go"  # sendRequest -> send_request
    assert c.retry_count() == 3               # retryCount  -> retry_count


def test_field_is_snake_cased(styling: ModuleType) -> None:
    c = styling.HttpClient("http://x")
    assert c.base_url == "http://x"           # baseUrl -> base_url
    c.base_url = "http://y"
    assert c.send_request() == "http://y/go"


def test_static_method_is_snake_cased(styling: ModuleType) -> None:
    assert styling.HttpClient.default_port() == 8080  # defaultPort -> default_port


def test_weld_as_is_verbatim_and_per_language(styling: ModuleType) -> None:
    c = styling.HttpClient()
    # The Python override wins verbatim — a snake_case style could never emit this.
    assert c.PING() == "pong"
    # Neither the styled spelling nor the Lua-only override name leaks into Python.
    attrs = public_attrs(c)
    assert "do_ping" not in attrs
    assert "doPing" not in attrs
    assert "ping" not in attrs


def test_original_cpp_spellings_are_gone(styling: ModuleType) -> None:
    attrs = public_attrs(styling.HttpClient())
    for camel in ("sendRequest", "retryCount", "baseUrl", "defaultPort"):
        assert camel not in attrs