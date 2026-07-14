"""Cookbook 06 — assert the bound template instantiations of the `boxes` module."""

import boxes


def main() -> None:
    # Two instantiations of one annotated template, bound under explicit names.
    a = boxes.IntBox("apples", 3)
    b = boxes.IntBox("pears", 5)
    assert a.value == 3
    assert a.describe() == "apples: present"

    t = boxes.TextBox("motto", "weld it")
    assert t.value == "weld it"

    # The primary template's annotations reached every instantiation.
    assert "labelled box" in boxes.IntBox.__doc__
    assert "labelled box" in boxes.TextBox.__doc__

    # A function-template instantiation, bound via substitute().
    boxes.swap_int_boxes(a, b)
    assert (a.label, a.value) == ("pears", 5)
    assert (b.label, b.value) == ("apples", 3)

    print("cookbook 06-templates: OK")


if __name__ == "__main__":
    main()