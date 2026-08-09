// The C# bindings generator over the SHARED namespace cases
// (tests/common/cpp/namespace.hpp, welded for lang::cs) — via the header's
// register_ helpers, covering the namespace sweep (catalog), the semi-manual
// weld_function/weld_variable route (manual) and the tack-welding carriage
// over the unmarked `foreign` libraries (greedy, protected-knob, bound_into).
#include "shared_seam.hpp"
#include <fstream>

int main(int argc, char** argv) {
    namespace wcs = ::welder::rods::csharp;
    wcs::options opts{};
    opts.library = "welder_test_cs_namespace";
    opts.shim_include = "shared_seam.hpp";
    opts.cs_namespace = "namespace_cases";
    std::ofstream shim{argc > 1 ? argv[1] : "shim.cpp"};
    std::ofstream cs{argc > 2 ? argv[2] : "Bindings.cs"};
    wcs::document doc{};
    doc.opts = std::move(opts);
    wcs::module_writer m{&doc, ""};
    register_namespace(m);
    register_freestanding(m);
    register_foreign(m);
    shim << doc.render_shim();
    cs << doc.render_cs();
    return 0;
}
