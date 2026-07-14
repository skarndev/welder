"""Cookbook 08 — assert the tack-welded (unannotated) `vecmath` library."""

import math

import fastvec


def main() -> None:
    # A plain third-party struct, bound without a single annotation.
    a = fastvec.Vec3(1.0, 0.0, 0.0)  # synthesized aggregate constructor
    b = fastvec.Vec3(0.0, 1.0, 0.0)
    assert a.dot(b) == 0.0
    assert math.isclose((a + b).norm(), math.sqrt(2.0))

    # Free functions and constants come along...
    c = fastvec.cross(a, b)
    assert c == fastvec.Vec3(0.0, 0.0, 1.0)  # operator== -> __eq__
    assert fastvec.EPSILON == 1e-9

    # ...and nested namespaces recurse greedily into submodules.
    assert math.isclose(fastvec.units.to_degrees(math.pi), 180.0)

    print("cookbook 08-tack-welding: OK")


if __name__ == "__main__":
    main()