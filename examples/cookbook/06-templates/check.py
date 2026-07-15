"""Cookbook 06 — assert the bound template instantiations of the `boxes` module."""

import boxes


def main() -> None:
    # Route 1: alias-declared instantiations, bound by the namespace sweep under
    # their aliases' names — no name strings anywhere.
    a = boxes.IntBox("apples", 3)
    b = boxes.IntBox("pears", 5)
    assert a.value == 3
    assert a.describe() == "apples: present"

    t = boxes.TextBox("motto", "weld it")
    assert t.value == "weld it"

    # The bare template itself never binds — only its aliased instantiations do.
    assert not hasattr(boxes, "Box")

    # Route 2: a directly-welded instantiation under an explicit verbatim name.
    r = boxes.RealBox("pi", 3.14)
    assert r.value == 3.14

    # The primary template's annotations reached every instantiation, on both routes.
    assert "labelled box" in boxes.IntBox.__doc__
    assert "labelled box" in boxes.TextBox.__doc__
    assert "labelled box" in boxes.RealBox.__doc__

    # A function-template instantiation, bound via substitute().
    boxes.swap_int_boxes(a, b)
    assert (a.label, a.value) == ("pears", 5)
    assert (b.label, b.value) == ("apples", 3)

    print("cookbook 06-templates: OK")


if __name__ == "__main__":
    main()