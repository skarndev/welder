#pragma once

/** @file
    The low-level consteval vocabulary of the C#/.NET backend, as one include.

    This header is an **umbrella**: the material it used to hold verbatim now
    lives in focused files, and this include exists so that a translation unit
    (or a reader) can still ask for "the C# rod's type machinery" in one line.

    | Include | What it holds |
    |---|---|
    | `<welder/rods/csharp/text.hpp>` | pure string helpers (XML-doc comments, keyword escaping) |
    | `<welder/rods/csharp/reflect/symbols.hpp>` | the C-symbol / C++-qualified / unique-token spellings of an entity |
    | `<welder/rods/csharp/reflect/lookup.hpp>` | the member-lookup layer the generator and the shim agree through |
    | `<welder/rods/csharp/marshal/families.hpp>` | the std type families (optional, sequences, maps, tuples, smart pointers, expected) |
    | `<welder/rods/csharp/marshal/classify.hpp>` | @ref welder::rods::csharp::marshal_kind + `classify` + the unmarshallable gate |
    | `<welder/rods/csharp/marshal/spellings.hpp>` | the C-ABI ⇄ C# spelling pairs and the native-caster oracle |
    | `<welder/rods/csharp/marshal/ownership.hpp>` | `rv::` → @ref welder::rods::csharp::handle_return resolution |

    Everything in them is shared between the **generator** TU (which computes
    lookup indices while emitting) and the **shim** TU (which re-runs the same
    lookups to splice the exact overload) — one definition, two call sites,
    agreement by construction. A lookup that no longer matches (the header
    changed between generation and shim compilation) throws a constexpr
    exception, failing the shim build loudly instead of calling the wrong
    overload.

    Requires the welder vocabulary first (`#include <welder/vocabulary.hpp>`),
    like the rest of the reflection layer.
*/

#include <welder/rods/csharp/marshal/classify.hpp>
#include <welder/rods/csharp/marshal/families.hpp>
#include <welder/rods/csharp/marshal/ownership.hpp>
#include <welder/rods/csharp/marshal/spellings.hpp>
#include <welder/rods/csharp/reflect/lookup.hpp>
#include <welder/rods/csharp/reflect/symbols.hpp>
#include <welder/rods/csharp/text.hpp>
