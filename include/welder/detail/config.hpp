#pragma once
//
// welder is a header-only library. It can optionally be consumed as a single
// C++20 module (`import welder;`) built from modules/welder.cppm.
//
// The vocabulary headers tag their public namespace with WELDER_EXPORT. When a
// header is pulled into the module interface unit, modules/welder.cppm defines
// WELDER_EXPORT to `export`; in ordinary header-only use it expands to nothing.
//
// Why only the *vocabulary* is modularized: reflection (`<welder/reflect.hpp>`)
// and backends (`<welder/python.hpp>`) depend on <meta> and pybind11, which pull
// the standard library in textually and do not coexist with gcc-16 module units
// (importing such a module conflicts with a consumer's own textual std
// includes). Those layers stay header-only. See CLAUDE.md.

#ifndef WELDER_EXPORT
#  define WELDER_EXPORT
#endif
