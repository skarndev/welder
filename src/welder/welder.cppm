// The single C++20 module welder ships (Boost-style modularization).
//
// welder is fundamentally a header-only library; this is a thin wrapper that
// re-exports the std-free annotation vocabulary so users can write
// `import welder;` instead of including the headers. Only the vocabulary is
// exported: reflection and backends depend on <meta>/pybind11, which do not
// coexist with module units on gcc-16 (see welder/detail/config.hpp).
//
// The vocabulary headers carry WELDER_EXPORT on their namespace; defining it to
// `export` here turns the included declarations into the module's interface.

export module welder;

#define WELDER_EXPORT export
#include <welder/lang.hpp>
#include <welder/annotations.hpp>
