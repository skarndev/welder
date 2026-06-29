#pragma once
// Inheritance — mirrors tests/test_inheritance.py (same sections, same order).
//
// register_inheritance binds bases before the types that derive from them
// (pybind11 requires a native base to be registered first).
//
// #included by bindings.cpp after the welder vocabulary + pybind11 backend.

// --- native inheritance from a welded base ----------------------------------
struct [[=welder::weld(welder::lang::py)]]
Base {
    int base_field{1};
    int base_method() const { return base_field; }
    [[=welder::mark::exclude]] int base_secret{0}; // excluded on the base itself
};

struct [[=welder::weld(welder::lang::py)]]
Derived : public Base {                            // issubclass(Derived, Base)
    int derived_field{2};
    int derived_method() const { return derived_field; }
};

// A multi-level welded chain (Leaf -> Mid -> Base): every level stays a Python
// superclass and contributes its members through the MRO.
struct [[=welder::weld(welder::lang::py)]]
Mid : public Base {
    int mid_field{5};
};
struct [[=welder::weld(welder::lang::py)]]
Leaf : public Mid {
    int leaf_field{6};
};

// --- flattening of a non-welded mixin base ----------------------------------
struct Mixin {                                     // NOT welded -> not its own type
    int mixin_field{3};
    int mixin_method() const { return mixin_field; }
    [[=welder::mark::exclude]] int mixin_secret{0}; // marks honored when flattened
};

struct [[=welder::weld(welder::lang::py)]]
WithMixin : public Mixin {
    int own_field{4};
};

// --- a welded base reached only through a non-welded base --------------------
// Welded <- Bridge (non-welded, flattened) <- Through. The link to Welded must
// survive the non-welded bridge.
struct [[=welder::weld(welder::lang::py)]]
Welded {
    int welded_field{10};
    int welded_method() const { return welded_field; }
};
struct Bridge : public Welded {                    // NOT welded -> flattened
    int bridge_field{11};
};
struct [[=welder::weld(welder::lang::py)]]
Through : public Bridge {
    int through_field{12};
};

// --- a virtual diamond, all welded ------------------------------------------
// Apex <- (virtual) Left, Right <- Bottom. The shared virtual base is listed
// once; every level stays reachable.
struct [[=welder::weld(welder::lang::py)]]
Apex {
    int apex_field{20};
};
struct [[=welder::weld(welder::lang::py)]]
Left : public virtual Apex {
    int left_field{21};
};
struct [[=welder::weld(welder::lang::py)]]
Right : public virtual Apex {
    int right_field{22};
};
struct [[=welder::weld(welder::lang::py)]]
Bottom : public Left, public Right {
    int bottom_field{23};
};

inline void register_inheritance(pybind11::module_& m) {
    // Native bases must be registered before the types that derive from them.
    welder::pybind11::bind<Base>(m);
    welder::pybind11::bind<Derived>(m);
    welder::pybind11::bind<Mid>(m);
    welder::pybind11::bind<Leaf>(m);
    welder::pybind11::bind<WithMixin>(m);
    welder::pybind11::bind<Welded>(m);
    welder::pybind11::bind<Through>(m);
    welder::pybind11::bind<Apex>(m);
    welder::pybind11::bind<Left>(m);
    welder::pybind11::bind<Right>(m);
    welder::pybind11::bind<Bottom>(m);
}
