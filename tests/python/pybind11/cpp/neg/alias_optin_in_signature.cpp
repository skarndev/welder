// Negative-compile case (must FAIL to build): an alias-OPT-IN template (the
// weld lives on the namespace-scope alias, not the template) binds — but the
// bindability gate's registration oracle is a pure predicate of DECLARATIONS
// and cannot see a weld on an alias (an alias is unrecoverable from the type it
// names). So a signature naming the instantiation itself fails the gate, even
// though the same sweep registers it. This is the documented namespace-alias
// blind spot; the remedy is the type-level trust hatch
// (`welder::trust_bindable<vendor::Buf<int>> = true`), locked as WORKING by the
// positive twin in tests/common/cpp/templates.hpp (Pack::twin).
//
// Built by the `negcompile.alias_optin_in_signature` CTest, which expects
// failure. The TU is otherwise valid, so the only thing that can fail is the
// bindability static_assert.
#include <welder/vocabulary.hpp>

#include <pybind11/pybind11.h>

#include <welder/rods/python/pybind11/rod.hpp>

namespace vendor {

template <class T>
struct Buf { // third-party-style: no weld anywhere on the template
    T v{};
    Buf twin() const { return *this; } // names the instantiation itself
};

} // namespace vendor

namespace optin_ns {

// The alias-declaration's weld opts the instantiation in; twin()'s signature
// still fails the gate (no trust hatch here — that is the point).
using IntBuf [[=welder::weld(welder::lang::py)]] = vendor::Buf<int>;

} // namespace optin_ns

void bind_it(pybind11::module_& m) {
    welder::welder<welder::rods::pybind11::rod<>>::weld_namespace<^^optin_ns>(m);
}