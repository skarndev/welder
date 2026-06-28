// Test extension exercising every member-resolution case. Built twice — once
// consuming `import welder;` and once consuming welder header-only — from this
// single source, selected by WELDER_TEST_HEADER_ONLY. The module name comes from
// WELDER_TEST_MODNAME so each build produces a distinct importable module.
#include <cstdint>
#include <string>

#ifdef WELDER_TEST_HEADER_ONLY
#  include <welder/welder.hpp>
#else
import welder;
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/python.hpp>

// ---- automatic policy (the default) ----------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Automatic {
    int kept = 0;                                                  // bound
    [[=welder::mark::exclude]]                    int excl_all = 0; // excluded (all)
    [[=welder::mark::exclude(welder::lang::py)]]  int excl_py = 0;  // excluded (py)
    [[=welder::mark::exclude(welder::lang::lua)]] int excl_lua = 0; // excluded lua only -> kept for py
    [[=welder::mark::include(welder::lang::py)]]  int incl_py = 0;  // redundant under automatic -> kept
};

// ---- opt_in policy ----------------------------------------------------------
struct [[=welder::weld(welder::lang::py)]] [[=welder::policy::opt_in]]
OptIn {
    int unmarked = 0;                                              // not opted in -> not bound
    [[=welder::mark::include]]                    int incl_all = 0; // included (all) -> bound
    [[=welder::mark::include(welder::lang::py)]]  int incl_py = 0;  // included (py) -> bound
    [[=welder::mark::include(welder::lang::lua)]] int incl_lua = 0; // included lua only -> not bound for py
    [[=welder::mark::include(welder::lang::py)]]
    [[=welder::mark::exclude(welder::lang::py)]]  int incl_then_excl = 0; // exclude wins -> not bound
};

// ---- read/write roundtrip ---------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Values {
    int i = 0;
    double d = 0.0;
    std::string s;
};

// ---- constructors + methods -------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Counter {
    int count = 0;

    Counter() = default;
    Counter(int start) : count(start) {}          // overloaded constructor

    void increment() { ++count; }                 // method (mutating)
    void add(int n) { count += n; }               // method with a parameter
    int value() const { return count; }           // const method
    static int version() { return 7; }            // static method

    [[=welder::mark::exclude]] void secret() {}    // excluded -> not bound
};

// ---- overloaded methods -----------------------------------------------------
struct [[=welder::weld(welder::lang::py)]]
Calc {
    int base = 0;

    Calc() = default;
    Calc(int b) : base(b) {}

    int sum(int a) const { return base + a; }            // overload 1
    int sum(int a, int b) const { return base + a + b; } // overload 2
};

// ---- access control: only the public API is bound ---------------------------
struct [[=welder::weld(welder::lang::py)]]
Access {
    int visible = 0;             // public data   -> bound
    int read_hidden() const { return hidden; } // public method -> bound

private:
    int hidden = 9;              // private data  -> not bound
    void helper() {}             // private method-> not bound

protected:
    int guarded = 0;             // protected data-> not bound
};

// ---- inheritance: a welded base maps to native pybind11 inheritance ---------
struct [[=welder::weld(welder::lang::py)]]
Base {
    int base_field = 1;
    int base_method() const { return base_field; }
    [[=welder::mark::exclude]] int base_secret = 0; // excluded on the base itself
};

struct [[=welder::weld(welder::lang::py)]]
Derived : public Base {                            // issubclass(Derived, Base)
    int derived_field = 2;
    int derived_method() const { return derived_field; }
};

// ---- inheritance: a multi-level welded chain (Leaf -> Mid -> Base) -----------
struct [[=welder::weld(welder::lang::py)]]
Mid : public Base {
    int mid_field = 5;
};
struct [[=welder::weld(welder::lang::py)]]
Leaf : public Mid {
    int leaf_field = 6;
};

// ---- inheritance: a non-welded base is flattened in (mixin) ------------------
struct Mixin {                                     // NOT welded -> not its own type
    int mixin_field = 3;
    int mixin_method() const { return mixin_field; }
    [[=welder::mark::exclude]] int mixin_secret = 0; // marks honored when flattened
};

struct [[=welder::weld(welder::lang::py)]]
WithMixin : public Mixin {
    int own_field = 4;
};

// ---- inheritance: a welded base reached only THROUGH a non-welded base -------
// Welded (welded) <- Bridge (non-welded, flattened) <- Through (welded). The
// link to Welded must survive the non-welded bridge.
struct [[=welder::weld(welder::lang::py)]]
Welded {
    int welded_field = 10;
    int welded_method() const { return welded_field; }
};
struct Bridge : public Welded {                    // NOT welded -> flattened
    int bridge_field = 11;
};
struct [[=welder::weld(welder::lang::py)]]
Through : public Bridge {
    int through_field = 12;
};

// ---- inheritance: a virtual diamond, all welded -----------------------------
// Apex <- (virtual) Left, Right <- Bottom. The shared virtual base is listed
// once; every level stays reachable.
struct [[=welder::weld(welder::lang::py)]]
Apex {
    int apex_field = 20;
};
struct [[=welder::weld(welder::lang::py)]]
Left : public virtual Apex {
    int left_field = 21;
};
struct [[=welder::weld(welder::lang::py)]]
Right : public virtual Apex {
    int right_field = 22;
};
struct [[=welder::weld(welder::lang::py)]]
Bottom : public Left, public Right {
    int bottom_field = 23;
};

#ifndef WELDER_TEST_MODNAME
#  define WELDER_TEST_MODNAME welder_test_bindings
#endif

PYBIND11_MODULE(WELDER_TEST_MODNAME, m) {
    m.doc() = "welder test bindings";
    welder::py::bind<Automatic>(m);
    welder::py::bind<OptIn>(m);
    welder::py::bind<Values>(m);
    welder::py::bind<Counter>(m);
    welder::py::bind<Calc>(m);
    welder::py::bind<Access>(m);
    // Native bases must be registered before the types that derive from them.
    welder::py::bind<Base>(m);
    welder::py::bind<Derived>(m);
    welder::py::bind<Mid>(m);
    welder::py::bind<Leaf>(m);
    welder::py::bind<WithMixin>(m);
    welder::py::bind<Welded>(m);
    welder::py::bind<Through>(m);
    welder::py::bind<Apex>(m);
    welder::py::bind<Left>(m);
    welder::py::bind<Right>(m);
    welder::py::bind<Bottom>(m);
}
