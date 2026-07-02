// Shadow-header generator for the docs-pipeline golden test: reflect over the
// corpus namespace and emit its Doxygen shadow header — to the file named by
// argv[1], or stdout. This is also the shape of a real user's doc generator: one
// tiny TU, built with the same toolchain as the bindings, run at build time
// (like the pybind11-stubgen step).
#include <welder/welder.hpp>       // vocabulary + reflection (header-only form)
#include <welder/docs/doxygen.hpp> // doc walker + Doxygen shadow-header emitter

#include "corpus.hpp"

#include <cstdio>
#include <fstream>
#include <ios>
#include <string>

int main(int argc, char** argv) {
    const std::string text{welder::docs::shadow_header<^^atelier>()};
    if (argc > 1) {
        std::ofstream f{argv[1], std::ios::binary};
        f << text;
        if (!f.good()) {
            std::fprintf(stderr, "welder_docs_gen: cannot write %s\n", argv[1]);
            return 1;
        }
        return 0;
    }
    std::fputs(text.c_str(), stdout);
    return 0;
}
