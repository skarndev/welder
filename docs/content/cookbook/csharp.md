# 11 — C# / .NET bindings

*Source: [`examples/cookbook/11-csharp`][src].*

C# has no in-process registration API a native module could call, so the C# rod is
a **build-time** backend (like the LuaCATS stubs or the trampoline generator): one
reflection pass emits an `extern "C"` shim — compiled into a native shared
library — and a `[LibraryImport]` C# wrapper that P/Invokes it. This recipe drives
the whole loop from a consumer project: generate, build the native library,
compile the wrapper into a .NET app, assert the surface, and pack a NuGet package.

## The pieces

**The types** (`inventory.hpp`) — one welded class with a live container member, a
root-level free function, and a nested namespace:

```cpp
namespace inventory {

struct [[=welder::weld(welder::lang::cs)]]
[[=welder::doc("A shipping crate.")]]
Crate {
    std::string label{};
    std::int32_t weight{0};
    std::vector<std::int32_t> serials{};   // a LIVE member on the C# side

    Crate() = default;
    Crate(std::string l, std::int32_t w);
    std::int32_t serial_total() const;
};

[[=welder::weld(welder::lang::cs)]]
std::int32_t combined_weight(const Crate& a, const Crate& b);

namespace audit {
[[=welder::weld(welder::lang::cs)]]
std::string stamp(const Crate& c);         // -> inventory.Audit.Global.Stamp
}

} // namespace inventory
```

**The generator** (`gen.cpp`) — one macro line:

```cpp
#include "inventory.hpp"
#include <welder/rods/csharp/module.hpp>

WELDER_CSHARP_MAIN(inventory, "inventory.hpp", "cookbook_inventory")
```

**The build** — `welder_csharp_generate_bindings()` builds and runs the generator,
then compiles the emitted shim into the native library; `welder_csharp_nuget_project()`
wraps the same pair into a packable csproj:

```cmake
welder_csharp_generate_bindings(inventory_cs
  SOURCES gen.cpp
  LIBRARY cookbook_inventory
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS inventory.hpp)

welder_csharp_nuget_project(inventory_cs
  PACKAGE_ID Cookbook.Inventory
  VERSION 0.1.0)
```

**The consumer** (`Check.cs`) — the generated surface reads like a hand-written
.NET library:

```csharp
using inventory;

using var crate = new Crate("bolts", 12);          // SafeHandle + IDisposable
var serials = crate.Serials;                        // a live view of the member
serials.Add(100);                                   // push_back on the C++ vector
var span = serials.AsSpan();                        // zero-copy Span<int> over data()
span[0] = 500;                                      // writes the C++ buffer

int w = Global.CombinedWeight(crate, other);        // root free fn -> Global
string s = inventory.Audit.Global.Stamp(other);     // nested ns -> real C# namespace
```

## Distribution: the NuGet package

`dotnet pack Cookbook.Inventory.csproj -c Release` yields a standard package —
the managed assembly under `lib/<tfm>/` and the native library under
`runtimes/<rid>/native/`, the layout .NET's runtime probing resolves
automatically on the consumer's machine:

```
Cookbook.Inventory.0.1.0.nupkg
├── lib/net10.0/Cookbook.Inventory.dll
└── runtimes/osx-arm64/native/libcookbook_inventory.dylib
```

The RID is the build platform's; a multi-platform package is produced by packing
on each platform and merging the `runtimes/` trees (or publishing per-RID
packages from a CI matrix).

## Run it

```console
$ ctest --test-dir build/cookbook -R csharp --output-on-failure
    Start 13: cookbook.11-csharp
1/2 Test #13: cookbook.11-csharp ...............   Passed
    Start 14: cookbook.11-csharp-pack
2/2 Test #14: cookbook.11-csharp-pack ..........   Passed
```

The full feature surface — exceptions, ownership, operators, inheritance,
C#-overridable virtuals, the container families — is the guide's
[C# / .NET bindings](../guide/csharp.md) page.

[src]: https://github.com/skarndev/welder/tree/main/examples/cookbook/11-csharp
