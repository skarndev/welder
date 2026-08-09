// The C# bindings generator: one macro line; welder_csharp_generate_bindings()
// (CMake) builds it, runs it, and compiles the emitted shim into the native
// library the [LibraryImport] wrapper P/Invokes.
#include "inventory.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(inventory, "inventory.hpp", "cookbook_inventory")
