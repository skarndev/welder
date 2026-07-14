// Cookbook 03 — inheritance: native bases vs mixins.
//
// `weld` is a DISCOVERY marker, not an inheritance directive. A welded base
// becomes a native base class in the target language (issubclass holds); a
// non-welded base is a C++ mixin whose eligible members are flattened into the
// derived binding; and a welded ancestor reached only through a non-welded
// bridge still becomes the native base. docs/content/cookbook/inheritance.md
// walks through this file.
#include <string>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/module.hpp>

namespace
[[=welder::doc("Vehicles, three inheritance flavors.")]]
garage {

// A welded base: bound as its own Python class. (Namespace binding visits
// declarations in order, so the base is registered before anything derives
// from it — pybind11 requires that.)
struct
[[=welder::weld(welder::lang::py), =welder::doc("Anything that rolls.")]]
Vehicle {
    int wheels{4};

    std::string describe() const { return "rolls on " + std::to_string(wheels); }
};

// Welded base + welded derived -> NATIVE inheritance: Python's
// issubclass(Car, Vehicle) holds and Vehicle's members arrive via the MRO.
struct
[[=welder::weld(welder::lang::py)]]
Car : public Vehicle {
    int doors{4};
};

// NOT welded -> a mixin. It never becomes a Python class of its own; welded
// types deriving from it get its eligible members FLATTENED in (its own marks
// still honored — service_secret stays out).
struct Serviceable {
    int last_service_year{2026};

    std::string service() { return "serviced"; }

    [[=welder::mark::exclude]]
    int service_secret{0};
};

// One native base (Vehicle) + one flattened mixin (Serviceable).
struct
[[=welder::weld(welder::lang::py)]]
Truck : public Vehicle, public Serviceable {
    double payload_tons{7.5};
};

// A welded ancestor reached only THROUGH a non-welded bridge: the bridge is
// flattened, and the link to the welded ancestor survives — issubclass(Racer,
// Chassis) still holds.
struct
[[=welder::weld(welder::lang::py)]]
Chassis {
    int frame_id{100};
};

struct Prototype : public Chassis { // not welded -> flattened bridge
    int lab_only{1};
};

struct
[[=welder::weld(welder::lang::py)]]
Racer : public Prototype {
    int top_speed{300};
};

} // namespace garage

WELDER_MODULE(garage, pybind11) {}