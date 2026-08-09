// The C# bindings generator over the SHARED nested-type cases
// (tests/common/cpp/nested.hpp, welded for lang::cs) — via the header's own
// register_nested() so the manual flat-registration escape (Robot::Beacon ->
// RobotBeacon) is exercised alongside the sweep.
#include "shared_seam.hpp"
#include <fstream>

int main(int argc, char** argv) {
    namespace wcs = ::welder::rods::csharp;
    wcs::options opts{};
    opts.library = "welder_test_cs_nested";
    opts.shim_include = "shared_seam.hpp";
    opts.cs_namespace = "nested_cases";
    std::ofstream shim{argc > 1 ? argv[1] : "shim.cpp"};
    std::ofstream cs{argc > 2 ? argv[2] : "Bindings.cs"};
    wcs::document doc{};
    doc.opts = std::move(opts);
    wcs::module_writer m{&doc, "Global"};
    register_nested(m);
    shim << doc.render_shim();
    cs << doc.render_cs();
    return 0;
}
