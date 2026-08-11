# Task: what wowlib still cannot bind to C#

**All three gaps in the previous revision of this list are closed** (2026-08-11).
What remains is to re-run wowlib against them and delete the twelve
`mark::exclude(lang::cs)` lines.

## What changed in welder

| Was excluded | Now |
|---|---|
| `MapChunk::alpha_maps`, `M2Track<T>::values` / `timestamps`, `M2TrackBase::timestamps`, `WDTParticulates::point_groups` / `bound_groups` (`vector<vector<T>>`) | bind — the outer is `seq_ref`, each element a live view of the inner sequence's own wrapper |
| `M2SkinProfile::bones`, `M2ChunkedFile::texture_ac` (`vector<array<T, N>>`) | bind — same, with the fixed-size inner wrapper |
| `FileSystem::enumerate_paths()`, `RoundtripReport::unknown_chunks` (`vector<string>`) | bind as C# `string[]` |
| `ChunkedFile::read(span<const byte>)` / `write()` (declared + flattened overloads collapsing onto one symbol) | bind — a flattened member's symbol is namespaced by its declaring scope, and its lookup goes through `base_scope` |

Details and rationale: `git log` on `feature/csharp` and the
"Nested value sequences" / "String sequences" / "FIXED" paragraphs in
`.claude/context/binding-features.md`.

The nested-sequence element surfaces managed-side as the inner wrapper, so an
ADT alpha map reads and writes zero-copy:

```csharp
var layer = chunk.AlphaMaps[0];      // a live VectorByte view
layer.AsSpan()[2048] = 0xFF;         // writes straight into the C++ buffer
byte[] copy = layer.ToArray();       // when a copy IS wanted
```

## Verifying against wowlib

```bash
cd ~/WoWModding/Projects/wowlib
# delete the twelve `=welder::mark::exclude(welder::lang::cs),` lines first
cmake -S . -B build/csharp -G Ninja -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_BUILD_TYPE=Release -DWOWLIB_BUILD_TESTS=OFF -DWOWLIB_BUILD_CSHARP=ON \
  -DFETCHCONTENT_SOURCE_DIR_WELDER=$PWD/../welder
cmake --build build/csharp --target wowlib_cs
```

The generator TU is ~7 min / ~3 GB; iterate on the shim alone with
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

welder's own C# tests no longer need a Python backend enabled: the tests/ tree is
added on `WELDER_BUILD_TESTS` alone, so `ctest -R csharp` works in a
C#-rod-only configure.

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
  nothing here. A `vector<vector<string>>` and a `span` of any non-blittable
  element join that list — both are designed errors, not oversights.
