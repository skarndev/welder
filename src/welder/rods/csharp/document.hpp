#pragma once

/** @file
    The C#/.NET backend's document assembler, as one include.

    This header is an **umbrella** over the two-artifact document and the RAII
    writer handles the driver's module/class/enum handles deduce to:

    | Include | What it holds |
    |---|---|
    | `<welder/rods/csharp/document/artifacts.hpp>` | @ref welder::rods::csharp::options, the shim + `Bindings.cs` buffers, the placeholder registries, the two `render_*` passes, and the module handle |
    | `<welder/rods/csharp/document/code_writer.hpp>` | the indentation-aware @ref welder::rods::csharp::code_writer + the @ref welder::rods::csharp::bound_symbol (one symbol → the coordinated thunk/P-Invoke/wrapper sinks) |
    | `<welder/rods/csharp/document/class_writer.hpp>` | the class handle: member accumulation, the comparison-pairing ledger, the CS0102 / reserved-name diagnostics |
    | `<welder/rods/csharp/document/enum_writer.hpp>` | the enum handle |
*/

#include <welder/rods/csharp/document/artifacts.hpp>
#include <welder/rods/csharp/document/class_writer.hpp>
#include <welder/rods/csharp/document/code_writer.hpp>
#include <welder/rods/csharp/document/enum_writer.hpp>
