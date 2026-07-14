"""Cookbook 03 — assert the three inheritance flavors of the `garage` module."""

import garage


def main() -> None:
    # Welded base + welded derived -> native inheritance.
    assert issubclass(garage.Car, garage.Vehicle)
    car = garage.Car()
    assert car.describe() == "rolls on 4"  # inherited through the MRO
    assert car.doors == 4

    # A non-welded mixin never becomes a class of its own...
    assert not hasattr(garage, "Serviceable")
    # ...but its eligible members are flattened into the welded derived type,
    # with the mixin's own marks honored.
    truck = garage.Truck()
    assert issubclass(garage.Truck, garage.Vehicle)  # the welded base stays native
    assert truck.last_service_year == 2026
    assert truck.service() == "serviced"
    assert not hasattr(truck, "service_secret")
    assert truck.payload_tons == 7.5

    # A welded ancestor reached through a non-welded bridge: the bridge is
    # flattened, the native link to the ancestor survives.
    assert not hasattr(garage, "Prototype")
    assert issubclass(garage.Racer, garage.Chassis)
    racer = garage.Racer()
    assert racer.frame_id == 100  # from the welded ancestor
    assert racer.lab_only == 1  # flattened from the bridge
    assert racer.top_speed == 300

    print("cookbook 03-inheritance: OK")


if __name__ == "__main__":
    main()