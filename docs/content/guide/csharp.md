# C# / .NET bindings

The C# rod (`welder::rods::csharp::rod`, language `welder::lang::cs`) targets
.NET over the one interop channel the CLR offers native code: **P/Invoke across a
C ABI**. Unlike CPython or Lua, C# has no in-process "register a class into a
live module" API a shared library could call at load time — so this backend is a
**build-time, text-emitting rod** (like the [LuaCATS stub rod](stubs.md)) that
emits *two coordinated artifacts* from one driver pass:

- **`shim.cpp`** — an `extern "C"` thunk per bound member, compiled into a
  shared library;
- **`Bindings.cs`** — the matching `[LibraryImport]` P/Invoke declarations plus
  idiomatic wrapper classes.

Both are written together per member, keyed by the same C symbol, so the native
and managed sides cannot drift apart. Member selection, overload grouping,
policy/mark resolution and the bindability gate are the *same* generic driver
every other rod uses — only the emission differs.

## The generated surface

```cpp
struct [[=welder::weld(welder::lang::cs)]]
[[=welder::doc("A 2-D integer point.")]]
Point {
    std::int32_t x{0};
    std::int32_t y{0};
    Point() = default;
    Point(std::int32_t x_, std::int32_t y_) : x{x_}, y{y_} {}
    void offset(std::int32_t dx, std::int32_t dy);
    void offset(std::int32_t d);              // an overload
    std::string label() const;
    [[=welder::getter]] std::int32_t depth() const;
    [[=welder::setter]] void depth(std::int32_t d);
};
```

becomes (under the default `dotnet` PascalCase style):

```csharp
using (var p = new Point(3, 4))       // ctor -> native new; IDisposable + SafeHandle
{
    p.X = 10;                         // field -> property
    p.Offset(1, 1);                   // overloads stay natural C# overloads
    p.Offset(2);
    string s = p.Label();             // std::string -> string (UTF-8)
    p.Depth = 9;                      // getter/setter marks -> a C# property
    using var q = p.Clone();          // the copy constructor's C# spelling
}
```

- A welded **class** wraps an opaque native handle held by a per-class
  `SafeHandle` (finalizer-safe: `Dispose()` or GC finalization releases through
  the native destructor, and every P/Invoke that passes the handle is protected
  against premature collection).
- **Fields** become properties; `const` or
  [`mark::no_reassign`](annotations.md) members get-only.
- **Overload groups** map to natural C# overloading — each overload gets its own
  C symbol, so the exact C++ overload is always called (no wire-side overload
  resolution).
- The admitted **copy constructor** becomes `Clone()` (C# has no copy-ctor
  protocol; a `T(other)` overload would collide with one-argument constructors).
- **Enums** mirror as `enum : <underlying>` with per-enumerator `///` docs —
  C# has the per-member doc slot Python lacks.
- **Docs** ride along as full XML doc comments: `[[=welder::doc]]` →
  `<summary>`, parameter docs → `<param>`, `[[=welder::returns]]` →
  `<returns>` — visible in IDE IntelliSense.

## Exceptions cross the boundary

Every thunk carries a trailing `welder_error*` out-parameter. A C++ exception is
caught in the shim's marshalling layer (never unwinding through the C ABI) and
rethrown managed-side, mapped onto the matching BCL type where one exists —
`std::invalid_argument` → `ArgumentException`, `std::out_of_range` →
`ArgumentOutOfRangeException`, `std::bad_alloc` → `OutOfMemoryException`,
overflow/underflow/range errors → `ArithmeticException` — and anything else as
`WelderNativeException`, always carrying the `what()` text:

```csharp
try { boundary.At(99); }
catch (ArgumentOutOfRangeException ex) { Console.WriteLine(ex.Message); }
```

## How the shim stays correct: splice, don't respell

The generated `shim.cpp` is compiled **with reflection enabled** against the
same welded header. Each thunk body is a one-liner delegating into a compiled
marshalling library, parameterized by the *exact member reflection* — re-derived
by a shared lookup (`named_member(^^geo::Point, "offset", 1)`), not respelled:

```cpp
void welder_geo_Point_m_offset_1(void* self, std::int32_t a0, welder_error* err)
{ return wcs::shim::method<^^::geo::Point,
      wcs::named_member(^^::geo::Point, "offset", 1)>(self, err, a0); }
```

Only the C-ABI wire types are text; parameter conversion, the call, exception
catching and return marshalling are ordinary compiled C++ (templates in
`<welder/rods/csharp/shim_support.hpp>`). If the welded header changes after
generation, the lookup fails the *shim build* with a designed diagnostic instead
of silently binding the wrong member.

## Building

```cmake
include(WelderCSharpModule)
welder_csharp_generate_bindings(mymod_csharp
  SOURCES gen.cpp                 # WELDER_CSHARP_MAIN(mymod, "mymod.hpp", "mymod_native")
  LIBRARY mymod_native
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
```

builds a generator, runs it (→ `shim.cpp` + `Bindings.cs`), and compiles the
shim into `libmymod_native.{so,dylib}` / `mymod_native.dll`. Compile the
generated `Bindings.cs` (path in the target's `WELDER_CSHARP_BINDINGS` property)
into any .NET 7+ project with the shared library next to the executable. The
generator TU:

```cpp
#include <welder/rods/csharp/module.hpp>
#include "mymod.hpp"
WELDER_CSHARP_MAIN(mymod, "mymod.hpp", "mymod_native")
```

## Marshalling rules (current phase)

| C++ | C ABI wire | C# |
|---|---|---|
| arithmetic scalars | fixed-width (`std::int32_t`, …) | `int` / `byte` / `double` / … |
| `bool` | `bool` | `bool` (`[MarshalAs(U1)]`) |
| `std::string` / `string_view` / `char*` | UTF-8 `const char*` in; malloc'd out (freed via `welder_free`) | `string` |
| welded enum | its underlying type | the mirrored `enum` |
| welded class (param) | opaque `void*` | the wrapper (its `SafeHandle`) |
| welded class (value/`&` return, default or `rv::copy`) | owned `void*` (heap copy — pybind11's `automatic`) | the wrapper, owning |
| welded class (`T*` return, default or `rv::take_ownership`) | adopted `void*` | the wrapper (owning), or `null` |
| welded class return under `rv::reference` / `reference_internal` | the object's address | a non-owning **view** |
| non-const welded-class **field** | the member's address | a live view (writes go through) |

## Ownership and views

The [`return_policy`](return-policies.md) annotation is honored exactly as on
the Python rods. A **view** wraps the same C++ object without owning it
(`Dispose` releases nothing); under `reference_internal` — and for every
class-typed field — the view also stores its parent in an internal `__owner`
reference, so the parent cannot be garbage-collected (and its C++ object
destroyed) while the view is reachable:

```csharp
var v = holder.Item();   // [[=welder::return_policy(rv::reference_internal)]]
v.X = 55;                // writes the C++ member through the view
holder = null!;
GC.Collect();            // holder is pinned by v.__owner — v stays valid
seg.Start.X = 100;       // fields are live views too: writes go through
```

A pointer return may be C# `null` (the wrapper type is `T?`); `keep_alive` is
documented-ignored (as on the Lua rods) — the owner-reference mechanism covers
the common case.

What the [bindability gate](bindability.md) admits but this phase cannot yet
marshal — STL containers, operators, welded-base inheritance, virtuals
overridden from C# — fails **loudly at generation time** with a designed
diagnostic naming the escape (`mark::exclude(welder::lang::cs)`), never a
silently-corrupting `void*`. Those families land in the following phases.
