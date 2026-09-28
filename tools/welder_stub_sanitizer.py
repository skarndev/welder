"""Normalize third-party stubgen output for stdlib-enum classes.

Both Python backends bind welded enums as REAL stdlib enums (nanobind
``is_arithmetic``/``is_flag``, pybind11 ``native_enum``), and both stub
generators render those classes by runtime introspection — which leaves two
artifacts a strict mypy rejects, both specific to how the enum machinery
populates the class dict:

1. nanobind's stubgen emits the runtime alias ``__str__ = __repr__`` BEFORE
   the ``__repr__`` def it references (enum aliases ``__str__`` to
   ``__repr__`` at class creation) — a name-defined error at class scope.
   The alias is dropped; the runtime class carries the real thing.

2. pybind11-stubgen renders enum members as annotations
   (``Bold: typing.ClassVar[Styles]``). mypy's enum semantics count only
   ASSIGNED names as members, so an ``enum.Flag`` subclass rendered that way
   is "an enum with zero members" (a misc error; plain IntEnum happens to be
   tolerated). Member annotations are rewritten to ``Bold = ...``
   assignments — the spelling mypy's own diagnostic asks for.

Usage: ``python welder_stub_sanitizer.py <stubs-dir>`` — rewrites ``.pyi``
files in place, only inside classes deriving an ``enum.*`` base.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

_CLASS = re.compile(r"^(\s*)class\s+(\w+)\(([^)]*)\):")
_ALIAS = re.compile(r"^\s*__str__ = __repr__\s*$")
_MEMBER = re.compile(r"^(\s*)(\w+): typing\.ClassVar\[[\w.\[\], ]+\]\s*$")


def sanitize_text(text: str) -> str:
    out: list[str] = []
    enum_indent: str | None = None  # inside an enum class when set
    for line in text.splitlines(keepends=True):
        m = _CLASS.match(line)
        if m is not None:
            bases = m.group(3)
            enum_indent = m.group(1) if "enum." in bases else None
            out.append(line)
            continue
        if enum_indent is not None:
            stripped = line.strip()
            # left the class body? (a non-blank line at or above the class's
            # own indentation)
            if stripped and not line.startswith(enum_indent + " ") \
                    and not line.startswith(enum_indent + "\t"):
                enum_indent = None
            elif _ALIAS.match(line):
                continue  # drop the forward alias (artifact 1)
            else:
                mm = _MEMBER.match(line)
                if mm is not None and not mm.group(2).startswith("_"):
                    out.append(f"{mm.group(1)}{mm.group(2)} = ...\n")
                    continue
        out.append(line)
    return "".join(out)


def main() -> int:
    root = Path(sys.argv[1])
    for pyi in root.rglob("*.pyi"):
        text = pyi.read_text(encoding="utf-8")
        fixed = sanitize_text(text)
        if fixed != text:
            pyi.write_text(fixed, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
