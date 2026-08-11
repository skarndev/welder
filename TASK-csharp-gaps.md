# Task: two gaps blocking a real library's C# bindings

Context: the C# rod was pointed at **wowlib** (`~/WoWModding/Projects/wowlib`), a
large reflection-heavy library — the whole version matrix of WoW file formats,
every fallible call returning `std::expected`. Seven bugs were found and fixed on
`feature/csharp` already (see `git log`, and `.claude/context/binding-features.md`
for the running notes). Two remain, and they are what stops wowlib's C# module
from linking.

Reproducing: wowlib builds the whole thing with

```bash
cd ~/WoWModding/Projects/wowlib
cmake -S . -B build/csharp -G Ninja -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_BUILD_TYPE=Release -DWOWLIB_BUILD_TESTS=OFF -DWOWLIB_BUILD_CSHARP=ON \
  -DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD/../welder
cmake --build build/csharp --target wowlib_cs
```

The generator TU takes ~7 min and ~3 GB; it currently SUCCEEDS, emitting a 4.4 MB
`Bindings.cs` and a 1.6 MB `shim.cpp`. Iterate on the shim alone — it reaches the
first error in well under a minute:

```bash
cd build/csharp && ninja -j1 bindings/CMakeFiles/wowlib_cs.dir/csharp/shim.cpp.o
```

Guard rails for both tasks — all must still hold:

```bash
cd ~/WoWModding/Projects/welder
# goldens must regenerate BYTE-IDENTICAL unless the task says otherwise
g++-16 -std=c++26 -freflection -O0 -Isrc -Itests/csharp/cpp -Itests/common/cpp \
  tests/csharp/cpp/gen.cpp -o /tmp/gen && /tmp/gen /tmp/shim.cpp /tmp/Bindings.cs
diff /tmp/shim.cpp tests/csharp/shim.golden.cpp
diff /tmp/Bindings.cs tests/csharp/Bindings.golden.cs
# the emitted shim must compile
g++-16 -std=c++26 -freflection -fsyntax-only -Isrc -Itests/csharp/cpp -Itests/common/cpp /tmp/shim.cpp
# consteval locks
for f in tests/core/*.cpp; do g++-16 -std=c++26 -freflection -fsyntax-only \
  -Isrc -Itests/common/cpp -Itests/csharp/cpp "$f" || echo "FAIL $f"; done
```

Note welder's own test suite is gated behind a Python backend
(`WELDER_BUILD_TESTS AND (PYBIND11 OR NANOBIND)` in the top-level CMakeLists), so
the C# tests cannot be reached without one — worth fixing separately, the C# tests
need neither.

---

## Task 1 — container element anchors for unspellable specializations

**84 of the 85 remaining shim errors.** One root cause.

```
shim_support.hpp: In instantiation of 'void* …shim::vec_new(welder_error*)
  [with std::meta::info E = ^^wowlib::formats::wmo::group::chunks::detail]':
shim_support.hpp:779:18: error: 'wowlib::formats::wmo::group::chunks::detail'
  is not usable in a splice type
```

`E` is a **namespace**, not a type. Distinct bad anchors observed:
`adt::detail`, `m2::root::record`, `m2::root::record::detail`, `m2::skin::detail`,
`wmo::group::detail`, `wmo::group::chunks::detail`.

**Why.** `rod::_ensure_vector<C>` (rod.hpp ~2029) spells the element anchor as

```cpp
const std::string eq{"^^" + std::string{cpp_name_v<bare(sequence_element(C))>}};
```

`cpp_name_v` is `qualified_cpp_name`, which walks parents collecting identifiers.
A class-template **specialization has no identifier**, so it contributes nothing
and the walk yields only the enclosing namespace path. The same mistake is at
`_ensure_array` (~2329, `targs`) and should be checked for `_ensure_map` (~2415)
and the `shared_ptr` box (~2585).

This is the third instance of one blind spot. The other two are already fixed and
are worth reading first as precedent: `symbol_token`/`symtok_v` (commit
`5c439fb`) for the C-symbol half, and `find_named_field`/`find_named_member` for
the lookup half.

**The mechanism that already works.** A welded class does not have this problem
because `make_class<T, Decl, …>` is handed `Decl` — the namespace-scope **welding
alias** (`^^wowlib::formats::adt::ADTTbc`), which IS spellable — and sets
`w.cpp_qualified = cpp_name_v<Decl>`. `_ensure_vector<C>` has only the container
type and never sees that alias.

**What to decide.** How the container generation reaches the element's alias
anchor. Options, roughly in order of how much machinery they reuse:

- Register the anchor spelling alongside the C# name. `make_class` already calls
  `doc.record_type_name(key, …)` keyed on `display_string_of`; add a parallel
  anchor registry and have `_ensure_*` emit a placeholder that the existing
  render-pass rescan resolves — the same deferral `container_ref`/`type_ref`
  already use (`\x01…\x02`, `\x03…\x04`). Watch ordering: containers are
  collected during the signature sweep, which may run before the element class is
  bound; the render pass is what makes that safe.
- Have the shim derive the type instead of spelling it — e.g. reach it through a
  member it appears in (`type_of(named_field(^^OwnerAlias, "field"))`). Avoids
  the alias entirely but needs an owner, which the container generator also does
  not currently have.
- Pass the element's `Decl` down into container collection (`_collect_containers`
  in every rod hook), which is the most invasive but the most direct.

**Acceptance.** wowlib's shim compiles past all 84; goldens byte-identical (the
existing cases use spellable elements, so nothing should move); add a test case
with an alias-welded specialization used as a `std::vector` element, which is the
shape none of `tests/csharp/cpp/cases.hpp` or `tests/common/cpp/templates.hpp`
currently covers.

---

## Task 2 — `std::vector<std::string>` marshalling

Not blocking the build (wowlib excludes the members), but it costs real API:
`FileSystem::enumerate_paths()` is the **only** file-listing call in wowlib, so a
C# caller can read any file it can already name but cannot discover what a client
contains. `RoundtripReport::unknown_chunks` is the other casualty. `string[]` is
an entirely ordinary .NET type, so this is welder being incomplete rather than an
impedance mismatch.

**Why it fails.** `type_map.hpp` `classify`, the sequence arm:

```cpp
const marshal_kind ek{classify(sequence_element(w))};
if (ek == scalar || ek == enum_) return seq_value;   // flat, blittable
if (ek == handle)                return seq_ref;     // opaque wrapper
return unsupported;                                  // utf8_string lands here
```

The wire is the obstacle: `welder_seq_wire { void* data; int64 len; }` describes
ONE contiguous blittable buffer that C# `Buffer.MemoryCopy`s wholesale. A
`std::vector<std::string>` is not blittable — each element is its own allocation.

**Shape of the work.**

- A string-sequence kind (new `marshal_kind`, or `seq_value` plus a flag).
- Wire: reuse `welder_seq_wire` with `data` as `const char**`.
- Shim return: malloc an array of `char*`, `dup_utf8` each element. Ownership is
  the fiddly part — the managed side must free every element AND the array.
- Shim param: managed pins an array of UTF-8 pointers; rebuild a
  `std::vector<std::string>`. Mirror how `seq_value` params pin (`call_pieces`
  `pin_open` / `needs_unsafe`).
- Generator: `public_type` → `"string[]"`, `pinvoke_type` → `WelderSeqWire`, and
  — the real work — `wrapper_return_body` / `append_one_param` must emit a
  per-element marshalling loop where `seq_value` currently emits a bulk copy.

**Acceptance.** A round-trip case covering `vector<string>` return and parameter,
empty vector, embedded non-ASCII (the UTF-8 path), and no leaks on either side;
then drop the two `mark::exclude(welder::lang::cs)` in wowlib
(`fs/filesystem.hpp` `enumerate_paths`, `audit/roundtrip.hpp` `unknown_chunks`)
and confirm they bind.

---

## Already fixed, do not redo

- gcc-16 **ICE** (segfault) splicing a flattened base member through the bound
  type — worked around in commit `837d3e3` by reaching the member through
  `parent_of(Mem)`. It is a plain compiler bug, reduced to 23 lines with neither
  welder nor wowlib involved: it needs BOTH a pointer type that is a splice of a
  class-TEMPLATE specialization AND a spliced member declared in a BASE; drop
  either and it compiles. Deterministic, independent of stack limit (8 MB and
  64 MB) and of optimization level. The reproducer and a Bugzilla draft are kept
  OUTSIDE this repo, at `~/WoWModding/gcc-splice-base-member-ice/`. Do not remove
  the workaround while the bug stands.
- The other five families and two specialization bugs — see `git log` on
  `feature/csharp` and `.claude/context/binding-features.md`.

## Known open, lower priority

An overload group mixing a member declared in the bound type with one flattened
from an unspellable-specialization base collapses onto one C symbol
(`..._m_read_0` twice) and one lookup. wowlib works around it by excluding
`ChunkedFile::read(span)`/`write()` for `cs`. `index_of_named_member` counts
within the DECLARING scope while the anchor falls back to the bound type; the fix
is to index over the same flattened sequence the lookup searches, which needs the
generator to know the anchor type at compile time — `add_method<Fns, Style>` does
not get it today (`class_writer` carries only runtime strings), so the carriage
would have to pass `BoundInto` through to the rod's `add_*` hooks.
