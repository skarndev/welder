// Negative-compile case (must FAIL to build): a VIRTUAL type nested inside a
// class-template specialization cannot have its trampoline generated — the
// enclosing specialization segment has no identifier, so the type's qualified
// C++ spelling cannot be recovered from reflection (it would truncate to broken
// text). The generator's spellability guard hard-errors instead of emitting it;
// the remedies are a hand-written trampoline spelled through the welding alias,
// or bind_flat.
//
// Built by the `negcompile.nested_virtual_in_specialization` CTest, which
// expects failure. The TU is otherwise valid, so the only thing that can fail
// is the cpp_spellable static_assert in the generator's document.
#include <welder/vocabulary.hpp>
#include <welder/rods/python/trampolines/rod.hpp>
#include <ostream>

namespace spec_nested_virtual {

template <class T>
struct [[=welder::weld(welder::lang::py)]] Boiler {
    // A nested virtual type: the sweep registers it under the alias-welded
    // instantiation, and the trampoline generator must refuse to respell it.
    struct Valve {
        virtual ~Valve() = default;
        virtual T flow() const { return T{}; }
    };
};

using IntBoiler = Boiler<int>;

} // namespace spec_nested_virtual

void generate_it(std::ostream& os) {
    welder::rods::trampolines::rod::generate<^^spec_nested_virtual>(os);
}