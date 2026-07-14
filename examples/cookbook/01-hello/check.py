"""Cookbook 01 — assert the bound surface of the `hello` module."""

import enum
import math

import hello


def main() -> None:
    # The type: fields, synthesized aggregate constructor, method, operators.
    v = hello.Vec2()
    v.x, v.y = 3.0, 4.0
    assert math.isclose(v.length(), 5.0)
    w = hello.Vec2(1.0, 2.0)  # synthesized positional constructor
    s = v + w  # operator+ -> __add__
    assert s == hello.Vec2(4.0, 6.0)  # operator== -> __eq__
    assert "2-D vector" in hello.Vec2.__doc__

    # The enum: a real enum.IntEnum, scoped values stay qualified.
    assert issubclass(hello.Color, enum.IntEnum)
    assert hello.Color.Green == 1
    assert not hasattr(hello, "Green")  # scoped enum: no module-level spill

    # The free function: keyword arguments from C++ parameter names, docstring
    # rendered Google-style with Args:/Returns: sections.
    mid = hello.midpoint(a=hello.Vec2(0.0, 0.0), b=hello.Vec2(2.0, 2.0))
    assert mid == hello.Vec2(1.0, 1.0)
    assert "Args:" in hello.midpoint.__doc__
    assert "Returns:" in hello.midpoint.__doc__

    # Namespace variables: TAU is a value snapshot; midpoint_count is a LIVE
    # property over the C++ global — a C++-side mutation is visible in Python,
    # and a Python-side assignment reaches C++.
    assert math.isclose(hello.TAU, 2.0 * math.pi)
    assert hello.midpoint_count == 0
    hello.count_midpoint()  # C++ increments the global
    assert hello.midpoint_count == 1
    hello.midpoint_count = 41  # Python writes the global...
    hello.count_midpoint()  # ...and C++ increments it again
    assert hello.midpoint_count == 42

    print("cookbook 01-hello: OK")


if __name__ == "__main__":
    main()