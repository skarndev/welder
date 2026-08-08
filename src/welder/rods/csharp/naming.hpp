#pragma once
#include <meta>
#include <string>

#include <welder/naming.hpp> // the name_style machinery + case restyling

/** @file
    The .NET naming convention used by welder's C# backend.

    The core name-styling layer (`<welder/naming.hpp>`) reshapes a C++ identifier
    into a target convention through one hook per entity kind; this header supplies
    the mix the [.NET framework design
    guidelines](https://learn.microsoft.com/dotnet/standard/design-guidelines/naming-guidelines)
    prescribe, so a generated C# binding reads idiomatically. Hand it to
    `welder::welder`:
    @code
    welder::welder<welder::rods::csharp::rod, welder::rods::csharp::dotnet>
        ::weld_namespace<^^mymod>(m);
    @endcode
    and `process_file` binds as `ProcessFile`, a `max_retries` field as the property
    `MaxRetries`, while a `GeometryHelper` class stays `GeometryHelper`. A
    `[[=welder::weld_as]]` on any entity still wins verbatim.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

namespace welder::inline v0::rods::csharp {

/** .NET naming: **PascalCase** for public members of every kind — types, enum types,
    methods, static methods, free functions, properties (data members), namespace
    variables and namespaces (submodules).

    Built by inheriting the all-PascalCase base and overriding only enum members,
    which are kept **verbatim**: C++ enumerators are already authored in the constant
    style the writer intends (`Red`, `MAX`, …), and reshaping them would be a
    surprising, lossy rename rather than a convention fix — the same choice
    `welder::rods::python::pep8` makes. (An `enum : <underlying>` value round-trips
    regardless of spelling.)

    @note PascalCase *normalizes* acronyms — `HTTPServer` → `HttpServer` — since words
    are lower-cased before re-capitalization. Idiomatic C++ class names are already
    PascalCase and pass through unchanged; reach for `[[=welder::weld_as]]` to pin an
    exact spelling. Satisfies @ref welder::naming::name_style. */
struct dotnet : ::welder::naming::pascal_case {
    /** Enum members → verbatim (see the type note). */
    static consteval std::string transform_enumerator(std::meta::info e) {
        return std::string{std::meta::identifier_of(e)};
    }
};

static_assert(::welder::naming::name_style<dotnet>);

} // namespace welder::inline v0::rods::csharp
