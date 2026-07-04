#pragma once
/** @file
    The `WELDER_EXPORT` macro — module-export marker for the vocabulary.

    welder is a header-only library. It can optionally be consumed as a single
    C++20 module (`import welder;`) built from `src/welder/welder.cppm`.

    `WELDER_EXPORT` expands to `export` inside the `welder` module interface unit
    and to nothing in ordinary header-only use. The vocabulary headers tag their
    public namespace with it; `src/welder/welder.cppm` defines it to `export` when
    it pulls those headers into the module purview.

    Why only the *vocabulary* is modularized: reflection (`<welder/reflect.hpp>`)
    and backends (`<welder/backends/python/pybind11/backend.hpp>`) depend on `<meta>` and
    pybind11, which pull the standard library in textually and do not coexist with
    gcc-16 module units (importing such a module conflicts with a consumer's own
    textual std includes). Those layers stay header-only. See CLAUDE.md.
*/

#ifndef WELDER_EXPORT
#  define WELDER_EXPORT
#endif
