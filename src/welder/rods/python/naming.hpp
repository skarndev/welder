#pragma once
#include <meta>
#include <string>

#include <welder/naming.hpp> // the name_style machinery + case restyling

/** @file
    The PEP 8 name style shared by welder's Python backends.

    The core name-styling layer (`<welder/naming.hpp>`) reshapes a C++ identifier
    into a target convention through one hook per entity kind; this header supplies
    the mix Python's [PEP 8](https://peps.python.org/pep-0008/) prescribes, so a
    Python binding reads idiomatically without every consumer re-spelling the rules.
    Hand it to `welder::welder`:
    @code
    welder::welder<welder::rods::pybind11::rod<>, welder::rods::python::pep8>
        ::weld_module<^^mymod>(m);
    @endcode
    and `processFile` binds as `process_file`, `MaxRetries` as a `MAX_RETRIES`-free
    `max_retries`, while a `GeometryHelper` class stays `GeometryHelper`. A
    `[[=welder::weld_as]]` on any entity still wins verbatim.

    Requires the welder vocabulary first (via `import welder;` or `#include
    <welder/vocabulary.hpp>`), like the rest of the reflection layer.
*/

namespace welder::rods::python {

/** PEP 8 naming: **CapWords** (PascalCase) for classes and enum types, **snake_case**
    for everything callable or data — methods, static methods, free functions, data
    members (properties), namespace variables — and submodules.

    Built by inheriting the all-snake_case base and overriding the two kinds PEP 8
    spells differently (types → CapWords) plus enum members, which are kept verbatim:
    C++ enumerators are already authored in the constant style the writer intends
    (`Red`, `MAX`, …), and Python's `enum` imposes no case, so reshaping them would be
    a surprising, lossy rename rather than a convention fix. (Whichever way an
    enumerator is spelled, `int(E.Value)` still works — the pybind11/nanobind rods bind
    `enum.IntEnum`.)

    @note "CapWords" here *normalizes* acronyms — `HTTPServer` → `HttpServer` — since
    the words are lower-cased before re-capitalization. Idiomatic C++ class names are
    already PascalCase and pass through unchanged; reach for `[[=welder::weld_as]]` to
    pin an exact spelling. Satisfies @ref welder::naming::name_style. */
struct pep8 : ::welder::naming::snake_case {
    /** Classes → CapWords. */
    static consteval std::string transform_class(std::meta::info e) {
        return ::welder::naming::restyle(std::meta::identifier_of(e),
                                         ::welder::naming::case_kind::pascal);
    }
    /** Enum types → CapWords. */
    static consteval std::string transform_enum(std::meta::info e) {
        return ::welder::naming::restyle(std::meta::identifier_of(e),
                                         ::welder::naming::case_kind::pascal);
    }
    /** Enum members → verbatim (see the class note). */
    static consteval std::string transform_enumerator(std::meta::info e) {
        return std::string{std::meta::identifier_of(e)};
    }
};

static_assert(::welder::naming::name_style<pep8>);

} // namespace welder::rods::python
