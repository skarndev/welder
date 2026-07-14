"""Cookbook 09 — assert the pruning tack weld of the `sensorlib` library."""

import math

import sensors


def main() -> None:
    # The public surface binds exactly as under plain tack welding.
    r = sensors.take_reading()
    assert math.isclose(r.celsius, 21.5)
    assert r.fresh()
    assert sensors.API_LEVEL == 3
    assert math.isclose(sensors.units.to_fahrenheit(0.0), 32.0)

    # The library's privacy convention is honored by the custom resolution:
    # underscore-prefixed entities of every kind stay out...
    assert not hasattr(sensors, "_CalibrationTable")  # a type
    assert not hasattr(sensors, "_reset_driver")  # a function
    assert not hasattr(sensors, "_debug_flag")  # a variable

    # ...and the private `detail` namespace is pruned wholesale — no submodule,
    # so nothing inside it (Driver, open_driver) was ever visited.
    assert not hasattr(sensors, "detail")

    print("cookbook 09-custom-traversal: OK")


if __name__ == "__main__":
    main()