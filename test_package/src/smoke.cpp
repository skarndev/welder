// Backend-free smoke test for the installed welder package: mark a type with the
// vocabulary and read welder's resolution back at compile time. If this compiles,
// the exported welder::headers target carried the vocabulary + <meta> to a consumer
// TU (the consumer supplies C++26 + -freflection itself, per its CMakeLists — welder
// checks those but does not propagate them).
#include <welder/vocabulary.hpp>  // the annotation vocabulary (lang / weld / policy)
#include <welder/welder.hpp>      // the core: entry point + welder::welded_for

struct [[=welder::weld(welder::lang::py)]]
       [[=welder::policy::automatic]] point {
    int x;
    int y;
};

static_assert(welder::welded_for(^^point, welder::lang::py),
              "welder must resolve point as welded for python");
static_assert(!welder::welded_for(^^point, welder::lang::lua),
              "point is welded for python only, not lua");

int main() { return 0; }