# Task: what wowlib still cannot bind to C#

The C# rod now builds wowlib end to end — `libwowlib_native.dylib` (10.3 MB, 6368
thunks) plus a 4.3 MB `Bindings.cs` that compiles, and a C# app that opens a real
3.3.5a MPQ chain, reads `DBFilesClient\Map.dbc`, and catches a `FileNotFound`
carrying wowlib's error text. Ten bugs were found and fixed getting there (see
`git log` on `feature/csharp` and `.claude/context/binding-features.md`).

What remains is **twelve members wowlib has to exclude for `lang::cs`**. Three
missing marshalling families and one rod bug account for all of them. They are
listed below in impact order — the first family is the one that costs real
capability.

## Reproducing

```bash
cd ~/WoWModding/Projects/wowlib
cmake -S . -B build/csharp -G Ninja -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_BUILD_TYPE=Release -DWOWLIB_BUILD_TESTS=OFF -DWOWLIB_BUILD_CSHARP=ON \
  -DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD/../welder
cmake --build build/csharp --target wowlib_cs
```

To see a gap's real diagnostic, delete the relevant
`=welder::mark::exclude(welder::lang::cs),` line and rebuild. The generator TU is
~7 min / ~3 GB; iterate on the shim alone with
`cd build/csharp && ninja -j1 bindings/CMakeFiles/wowlib_cs.dir/csharp/shim.cpp.o`.

Guard rails — all must hold after any change:

```bash
cd ~/WoWModding/Projects/welder
g++-16 -std=c++26 -freflection -O0 -Isrc -Itests/csharp/cpp -Itests/common/cpp \
  tests/csharp/cpp/gen.cpp -o /tmp/gen && /tmp/gen /tmp/shim.cpp /tmp/Bindings.cs
diff /tmp/shim.cpp tests/csharp/shim.golden.cpp        # byte-identical, or a
diff /tmp/Bindings.cs tests/csharp/Bindings.golden.cs  # deliberate, reviewed move
g++-16 -std=c++26 -freflection -fsyntax-only -Isrc -Itests/csharp/cpp -Itests/common/cpp /tmp/shim.cpp
for f in tests/core/*.cpp; do g++-16 -std=c++26 -freflection -fsyntax-only \
  -Isrc -Itests/common/cpp -Itests/csharp/cpp "$f" || echo "FAIL $f"; done
```

welder's own C# tests are unreachable without a Python backend enabled
(`WELDER_BUILD_TESTS AND (PYBIND11 OR NANOBIND)` in the top-level CMakeLists),
though they need neither — worth fixing while you are in here.

---

## 1. Nested value sequences — `vector<vector<T>>` and `vector<array<T, N>>`

**8 of the 12 exclusions, and the ones that cost capability.** Both shapes are one
missing family: a sequence whose element is itself a value sequence.

| member | type | what C# loses |
|---|---|---|
| `MapChunk::alpha_maps` | `vector<vector<uint8_t>>` | **ADT terrain texture blending** — the per-layer 64×64 alpha maps |
| `M2Track<T>::values` | `vector<vector<T>>` | **M2 animation keyframe values** (per sequence) |
| `M2Track<T>::timestamps` | `vector<vector<uint32_t>>` | M2 animation keyframe times |
| `M2TrackBase::timestamps` | `vector<vector<uint32_t>>` | event-track trigger times |
| `M2SkinProfile::bones` | `vector<array<uint8_t, 4>>` | **per-vertex bone indices** — no skinning |
| `M2ChunkedFile::texture_ac` | `vector<array<uint8_t, 2>>` | TXAC texture-transform flags (niche) |
| `WDTParticulates::point_groups` | `vector<vector<ParticulatePoint>>` | MPV particulate volumes (niche, BfA+) |
| `WDTParticulates::bound_groups` | `vector<vector<ParticulateBounds>>` | MPV particulate bounds (niche, BfA+) |

Without these a C# consumer can open a map tile but not read its texture
blending, and can load a model but not its animations or skinning — which is most
of what a viewer or editor exists to do. This is the one to do first.

**Why it fails.** `classify`'s sequence arm (`type_map.hpp`) admits an element
that is `scalar`/`enum_` (→ `seq_value`, a flat blittable buffer) or `handle`
(→ `seq_ref`, an opaque wrapper). A `vector<T>` or `array<T,N>` element is
neither, so it falls through to `unsupported`. `welder_seq_wire` describes ONE
contiguous buffer the managed side copies wholesale; a vector of vectors is a
vector of separate allocations.

**Note the inner element is always leaf-ish here** — scalars, or small welded
structs (`ParticulatePoint`). A general nested-container design is not required
to unlock the table above; one level of nesting over a `seq_value`-able inner
element covers every row.

**Acceptance.** Round-trip both shapes (jagged `vector<vector<T>>` and fixed
`vector<array<T,N>>`), including empty outer, empty inner, and mutation through
whatever the C# side exposes; then delete the eight excludes and confirm the
members bind and carry correct data for a real ADT and M2.

## 2. `vector<std::string>` → `string[]`

| member | cost |
|---|---|
| `FileSystem::enumerate_paths()` | **the only file-listing call in wowlib** — a C# caller can read any file it can already name, but cannot discover what a client contains |
| `RoundtripReport::unknown_chunks` | audit reporting only |

`string[]` is an entirely ordinary .NET type, so this is welder being incomplete
rather than an impedance mismatch. Same root cause as above — the element is not
blittable, so `welder_seq_wire`'s single-buffer model does not fit; here `data`
wants to be a `const char**` with per-element ownership on both sides (free every
element AND the array). The generator work is emitting a per-element marshalling
loop where `seq_value` currently emits a bulk copy (`wrapper_return_body` /
`append_one_param`).

## 3. Overload groups mixing a declared and a flattened member

| member | cost |
|---|---|
| `ChunkedFile::read(span<const byte>)` | no parse-from-memory in C# |
| `ChunkedFile::write()` | no serialize-to-memory in C# |

A **rod bug**, not a missing type. These are flattened onto every versioned
entity, which also welds a per-version `read(fs, key)` / `write(fs, key)` for
`cs` — so C# sees two `read` overloads whose declaring scopes are BOTH
class-template specializations. Those have no spellable name, so `_owner_expr`
falls back to the bound-type anchor for both and `index_of_named_member` counts
within each declaring scope, making both index 0: two thunks named
`..._m_read_0`, and a lookup that resolves both to the same member. welder's
duplicate-symbol `#error` catches it, so nothing is silent.

**The fix** is to index overloads over the same flattened sequence the shim-side
lookup searches (own members, then bases). That needs the generator to know the
ANCHOR type at compile time, which `add_method<Fns, Style>` does not get today —
`class_writer` carries only runtime strings — so the carriage would have to pass
`BoundInto` through to the rod's `add_*` hooks. The cheaper alternative is to emit
a scope discriminator (`symbol_token(parent_of(fn))`) into both the symbol and a
`base_scope(anchor, "<token>")` lookup, but that moves goldens for every
specialization-declared method.

The fs-level `read`/`write` are the pair C# keeps meanwhile — they are the only
way to load the multi-file entities (WMO groups, M2 satellites, split ADTs).

---

## Not gaps: two renames C# genuinely requires

These are welder's diagnostics working, and are already resolved in wowlib with
`weld_as` scoped to `lang::cs`, so the Python and Lua names are untouched. Listed
so nobody "fixes" them in welder.

- `SMOFog::Fog` (nested type) vs its `fog` member — both style to `Fog`, and C#
  forbids a type and member sharing a name (CS0102). The TYPE is renamed
  (`FogBand`), keeping the natural property spelling.
- `SMTextureColorGrading::_04` — a leading underscore survives PascalCase, and
  that namespace is reserved for welder's generated scaffolding. Renamed
  `Unknown04`. (The related welder bug — the aggregate ctor emitting `uint 04` —
  IS fixed; `weld_as` does not reach parameter names, which derive from the field
  identifiers.)

## Also worth knowing

- The gcc-16 ICE welder works around in `field_get` is a live compiler bug. A
  23-line reproducer and a Bugzilla draft live OUTSIDE this repo at
  `~/WoWModding/gcc-splice-base-member-ice/`. Do not remove the workaround.
- `keep_alive` is documented-ignored on this rod, as on the Lua rods.
- `std::variant`, class-keyed maps and custom-comparator maps remain
  unmarshallable by design/deferral; wowlib does not use them, so they cost
  nothing here.
