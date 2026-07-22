"""Cookbook 10 — assert the `scene` module's opaque, reference-semantic containers."""

import scene


def main() -> None:
    s = scene.Scene()

    # The generator emitted an opaque wrapper per container it found — derived names,
    # no hand-written WELDER_OPAQUE or aliases in the project.
    assert type(s.mesh).__name__ == "VectorVertex"
    assert type(s.actors).__name__ == "VectorEntity"
    assert type(s.weights).__name__ == "VectorDouble"

    # Reference semantics: a Python append is push_back on the SAME C++ vector — the
    # round-trip helpers read it back from C++.
    s.actors.append(scene.Entity("hero", 100.0))
    s.actors.append(scene.Entity("mob", 30.0))
    assert len(s.actors) == 2
    assert scene.actor_count(s) == 2  # C++ sees the appended entities
    assert s.actors[0].name == "hero"

    s.weights.append(1.5)
    s.weights.append(2.5)
    assert scene.total_weight(s) == 4.0  # writes reached C++

    # The by_value opt-out stays a plain list[int] (copied, not a live wrapper).
    assert isinstance(s.layers, list)
    s.layers = [1, 2, 3]
    assert s.layers == [1, 2, 3]

    # A container of a welded class is opaque too (not a list snapshot).
    assert not isinstance(s.actors, list)

    # Zero-copy numpy views (optional — only when numpy is installed; the views
    # themselves need no numpy at build/import time).
    try:
        import numpy as np
    except ImportError:
        print("cookbook 10-containers: OK (numpy not installed — array demo skipped)")
        return

    # A POD-struct vector -> a STRUCTURED array (named fields), zero-copy + writable.
    s.mesh.append(scene.Vertex())
    s.mesh.append(scene.Vertex())
    verts = np.asarray(s.mesh)
    assert verts.dtype.names == ("x", "y", "z")
    assert not verts.flags["OWNDATA"]  # a view over C++ memory
    verts[0]["x"] = 3.5
    assert s.mesh[0].x == 3.5  # numpy write reached the C++ struct

    # A scalar vector -> a plain typed array.
    w = np.asarray(s.weights)
    assert w.dtype == np.float64
    assert not w.flags["OWNDATA"]
    w[0] = 9.0
    assert scene.total_weight(s) == 11.5  # 9.0 + 2.5

    print("cookbook 10-containers: OK")


if __name__ == "__main__":
    main()
