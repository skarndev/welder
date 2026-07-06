#pragma once
// Inheritance — mirrors tests/test_inheritance.py (same sections, same order).
//
// register_inheritance binds bases before the types that derive from them
// (pybind11 requires a native base to be registered first).
//
// The cases live in namespace `inheritance`, bound under an `inheritance`
// submodule via WELDER_TEST_WELDER::weld_namespace so the Python package mirrors
// this file. bind_namespace visits members in declaration order, so each welded
// base is registered before the types that derive from it.
//
// #included by bindings.cpp after the welder vocabulary + the active Python backend.

namespace inheritance {

// --- native inheritance from a welded base ----------------------------------

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Base {
    int base_field{1};

    int base_method() const {
        return base_field;
    }

    // excluded on the base itself
    [[=welder::mark::exclude]]
    int base_secret{0};
};

// issubclass(Derived, Base)
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Derived : public Base {
    int derived_field{2};

    int derived_method() const {
        return derived_field;
    }
};

// A multi-level welded chain (Leaf -> Mid -> Base): every level stays a Python
// superclass and contributes its members through the MRO.
struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Mid : public Base {
    int mid_field{5};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Leaf : public Mid {
    int leaf_field{6};
};

// --- flattening of a non-welded mixin base ----------------------------------

// NOT welded -> not its own type
struct Mixin {
    int mixin_field{3};

    int mixin_method() const {
        return mixin_field;
    }

    // marks honored when flattened
    [[=welder::mark::exclude]]
    int mixin_secret{0};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
WithMixin : public Mixin {
    int own_field{4};
};

// --- a welded base reached only through a non-welded base --------------------
// Welded <- Bridge (non-welded, flattened) <- Through. The link to Welded must
// survive the non-welded bridge.

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Welded {
    int welded_field{10};

    int welded_method() const {
        return welded_field;
    }
};

// NOT welded -> flattened
struct Bridge : public Welded {
    int bridge_field{11};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Through : public Bridge {
    int through_field{12};
};

// --- a virtual diamond, all welded ------------------------------------------
// Apex <- (virtual) Left, Right <- Bottom. The shared virtual base is listed
// once; every level stays reachable.
//
// Guarded by WELDER_TEST_MULTIPLE_INHERITANCE: Bottom has two welded bases, which
// welder maps to two native base classes. pybind11 supports that; nanobind binds
// only single inheritance, so its backend defines this macro to nothing and the
// diamond is skipped (the Python spec skips its diamond cases when Bottom is
// absent). The single-base levels are guarded along with it, as they exist only to
// form the diamond.
#ifdef WELDER_TEST_MULTIPLE_INHERITANCE

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Apex {
    int apex_field{20};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Left : public virtual Apex {
    int left_field{21};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Right : public virtual Apex {
    int right_field{22};
};

struct
[[=welder::weld(welder::lang::py, welder::lang::lua)]]
Bottom : public Left, public Right {
    int bottom_field{23};
};

#endif // WELDER_TEST_MULTIPLE_INHERITANCE

} // namespace inheritance

inline void register_inheritance(WELDER_TEST_MODULE_T& m) {
    // Declaration order registers each native base before its derived types.
    auto sub{WELDER_TEST_SUBMODULE(m, "inheritance")};
    WELDER_TEST_WELDER::weld_namespace<^^inheritance>(sub);
}
