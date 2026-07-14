// Cookbook 02 — discovery: how welder decides what binds.
//
// One WELDER_MODULE line binds the whole `inventory` namespace; everything else
// is steered from the declarations — policies (automatic vs opt_in), marks
// (exclude / include), nested-namespace recursion (submodules; pruning `detail`),
// and weld_as renames. docs/content/cookbook/discovery.md walks through this file.
#include <cstdint>
#include <string>

#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <welder/rods/python/pybind11/module.hpp> // WELDER_MODULE for pybind11

// The namespace token doubles as the module name; its doc becomes the module
// docstring.
namespace
[[=welder::doc("A small warehouse inventory.")]]
inventory {

// automatic policy (the default): every public member binds unless excluded.
struct
[[=welder::weld(welder::lang::py), =welder::doc("A stocked article.")]]
Item {
    std::string name;
    int quantity{0};

    // Excluded from every language — implementation detail.
    [[=welder::mark::exclude]]
    std::uint64_t cache_key{0};

    [[=welder::doc("True once the shelf is empty.")]]
    bool depleted() const { return quantity <= 0; }
};

// weld_as forces the bound name VERBATIM (bypassing any name style): Python sees
// `Crate`, and the C++ spelling is not exposed.
struct
[[=welder::weld(welder::lang::py), =welder::weld_as("Crate")]]
BigWoodenBox {
    double capacity{100.0};
};

// No weld marker -> welder never binds it (weld is the discovery gate).
struct Ledger {
    int secret{0};
};

// opt_in policy: members bind only when explicitly included.
struct
[[
  =welder::weld(welder::lang::py),
  =welder::policy::opt_in,
  =welder::doc("An audit line; only the reviewed fields are exposed.")
]]
AuditRecord {
    int internal_id{0}; // not included -> not bound

    [[=welder::mark::include]]
    std::string note;

    [[=welder::mark::include]]
    int revision{0};
};

// Free functions participate like members; weld_as works on them too.
[[=welder::weld(welder::lang::py), =welder::doc("Quantity times unit price.")]]
double totalValue(const Item& item, double unit_price) {
    return item.quantity * unit_price;
}

// An overload set binds under one Python name with overload dispatch.
[[=welder::weld(welder::lang::py)]]
void restock(Item& item, int amount) { item.quantity += amount; }

[[=welder::weld(welder::lang::py)]]
void restock(Item& item) { item.quantity += 10; }

// A nested namespace with bound content becomes a submodule.
namespace pricing {

[[=welder::weld(welder::lang::py), =welder::doc("Apply a percentage discount.")]]
double discounted(double price, double percent) {
    return price * (1.0 - percent / 100.0);
}

} // namespace pricing

// Pruned wholesale: mark::exclude on a namespace stops recursion, even though
// the contents are welded — the usual way to keep `detail` / `impl` out.
namespace
[[=welder::mark::exclude]]
detail {

struct
[[=welder::weld(welder::lang::py)]]
ShelfIndex {
    int slot{0};
};

} // namespace detail

} // namespace inventory

// One line: emits PyInit_inventory and welds the namespace into it. The trailing
// block is optional post-glue with the module handle in scope as `module`.
WELDER_MODULE(inventory, pybind11) {
    module.attr("SCHEMA_VERSION") = 2;
}