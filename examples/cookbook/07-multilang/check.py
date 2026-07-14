"""Cookbook 07 — assert the Python face of the shared `journal` library.

Same library as check.lua asserts for Lua; compare the two side by side.
"""

import io
import os

import journal


def main() -> None:
    assert "journalling library" in journal.__doc__

    # PEP 8 style: camelCase C++ reshaped, classes stay PascalCase.
    e = journal.Entry("day 1", "welded")
    assert e.render_line() == "day 1: welded"
    assert not hasattr(e, "renderLine")

    nb = journal.Notebook()
    nb.add_entry(e)
    nb.add_entry(journal.make_entry(title="day 2"))
    assert nb.entry_count() == 2

    # Per-language weld_as: Python spells renderAll `to_text` (Lua: `as_string`).
    assert nb.to_text() == "day 1: welded\nday 2: (empty)\n"
    assert not hasattr(nb, "as_string")

    # The Python-flavored save_to (mark::only(py)) takes a file-like object —
    # anything with .write, e.g. io.StringIO or an open() handle.
    sink = io.StringIO()
    nb.save_to(sink)
    assert sink.getvalue() == nb.to_text()

    # Docstrings arrive Google-style (welder's doc/returns annotations).
    assert "dated journal entry" in journal.Entry.__doc__
    assert "Returns:" in journal.make_entry.__doc__

    # The style reshapes variables too: FORMAT_VERSION -> format_version.
    assert journal.format_version == 1

    # The .pyi stub (nanobind's bundled stubgen) carries the same surface.
    stub_path = os.environ["JOURNAL_PYI"]
    with open(stub_path, encoding="utf-8") as f:
        stub = f.read()
    assert "class Notebook:" in stub
    assert "def save_to(" in stub
    assert "def to_text(" in stub

    print("cookbook 07-multilang (python): OK")


if __name__ == "__main__":
    main()