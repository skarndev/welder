#pragma once

/** @file
    The compiled marshalling library the **generated** C# shim delegates into,
    as one include.

    The generated `shim.cpp` is compiled with reflection enabled against the same
    welded header the generator saw, so its thunk bodies need not respell any C++
    type: each is a one-liner delegating to a template here, parameterized by the
    exact member reflection re-derived through the shared lookup layer
    (`named_member` / `ctor_at` / `named_field` in
    `<welder/rods/csharp/reflect/lookup.hpp>`) — the splice-don't-respell idiom of
    the trampolines rod applied to the C ABI. Only the `extern "C"` **wire types**
    (`void*`, `std::int32_t`, `const char*`, `welder_error*`, …) and the entity
    anchor spellings (`^^ns::Type`, `"name"`) appear as text in the generated file.

    Every template here also owns the **error contract**: the call is wrapped in
    a catch-all whose result lands in the trailing @ref welder_error out-param
    (code `0` = success), so a C++ exception never unwinds through the C ABI.
    The managed wrapper checks the slot and throws after every P/Invoke.

    This header is an **umbrella** over the pieces:

    | Include | What it holds |
    |---|---|
    | `<welder/rods/csharp/shim/wire.hpp>` | the global wire structs, the error taxonomy, the catch boundary |
    | `<welder/rods/csharp/shim/convert.hpp>` | wire ⇄ C++ conversion (`to_cpp`, `guarded`) |
    | `<welder/rods/csharp/shim/entities.hpp>` | one thunk body per bound entity kind |
    | `<welder/rods/csharp/shim/containers.hpp>` | the generated container wrappers' operations |
    | `<welder/rods/csharp/shim/director_wire.hpp>` | the director callback's argument/result conversion |

    It also pulls in the two lookup layers the generated shim splices through —
    `named_operator` (`<welder/rods/csharp/operators.hpp>`) and `director_slot`
    (`<welder/rods/csharp/directors.hpp>`) — so a generated `shim.cpp` needs this
    one include and nothing else.
*/

#include <welder/rods/csharp/directors.hpp> // director_slot (the generated shim
                                            // re-derives virtual slots too)
#include <welder/rods/csharp/operators.hpp> // named_operator (the generated shim
                                            // splices operator lookups too)
#include <welder/rods/csharp/shim/containers.hpp>
#include <welder/rods/csharp/shim/convert.hpp>
#include <welder/rods/csharp/shim/director_wire.hpp>
#include <welder/rods/csharp/shim/entities.hpp>
#include <welder/rods/csharp/shim/wire.hpp>
