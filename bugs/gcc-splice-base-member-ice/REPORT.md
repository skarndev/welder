# GCC Bugzilla draft — ICE splicing a base-class member through a spliced template specialization

Paste-ready. `mre.cpp` next to this file is the reproducer.

---

**Summary**

`[c++26] ICE (segfault) splicing a base-class member on an object of spliced
class-template specialization type`

**Product / Component**: gcc / c++

**Version**: 16.1.0

---

**Description**

Splicing a non-static data member that is declared in a **base** class, where the
object expression has a type that is itself a **splice of a class-template
specialization**, segfaults the compiler.

Reduced from a real reflection-driven binding generator, where the pattern is
"recover the typed object from an opaque handle, then read a member the entity
inherits".

```cpp
// g++-16 -std=c++26 -freflection -fsyntax-only mre.cpp
#include <meta>

struct A { int m{}; };
template <int V> struct E : A {};

template <std::meta::info W, std::meta::info M>
int get(void* p) {
    auto* o{reinterpret_cast<[:W:]*>(p)};
    return (*o).[:M:];
}

int main() { return get<^^E<2>, ^^A::m>(nullptr); }
```

**Actual result**

```
mre.cpp: In instantiation of 'int get(void*) [with std::meta::info W = ^^E<2>; std::meta::info M = ^^A::m]':
mre.cpp:23:40:   required from here
mre.cpp:20:20: internal compiler error: Segmentation fault: 11
   20 |     return (*o).[:M:];
      |            ~~~~~~~~^~
```

**Expected result**

Compiles. The equivalent non-spliced code (`reinterpret_cast<E<2>*>(p)->m`) is
accepted.

---

**Both ingredients are required** — removing either compiles cleanly:

| variation | result |
|---|---|
| as above (template specialization + member from a base) | **ICE** |
| `struct E : A {};` — E not a template | OK |
| `template <int V> struct E { int m{}; };` — member declared in E itself | OK |
| `reinterpret_cast<E<2>*>(p)` — target type written, not spliced | OK |

Further notes:

- `static_cast<[:W:]*>(p)` ICEs identically, so it is not specific to
  `reinterpret_cast`.
- Taking the address (`auto& r{(*o).[:M:]}; return &r;`) rather than reading the
  value ICEs identically.
- Independent of the stack limit: reproduced with `ulimit -s` at both 8 MB and
  64 MB, always at the same point.
- Independent of optimization level (the reproducer uses `-fsyntax-only`).
- Deterministic.

---

**Environment**

```
gcc version 16.1.0 (Homebrew GCC 16.1.0)
Target: aarch64-apple-darwin25
cc1plus invocation (from the ICE report):
  -std=c++26 -freflection -fsyntax-only -mcpu=apple-m1 -mabi=lp64
```

Not yet checked on another host/target — worth confirming on x86-64 Linux before
filing, in case it is aarch64-specific.

---

## Workaround (what welder does)

Reach the member through its **declaring** class rather than through the derived
type, which is what the code means anyway — a no-op `static_cast` when the class
declares the member, and the explicit base adjustment when it does not:

```cpp
using Owner = [:std::meta::parent_of(M):];
auto* o{static_cast<Owner*>(reinterpret_cast<[:W:]*>(p))};
return (*o).[:M:];
```

Applied in `src/welder/rods/csharp/shim_support.hpp` (`field_get`), commit
`837d3e3`.
