# C#/.NET backend tests: generate the shim + C# wrapper from the welded cases,
# lock both against checked-in goldens, compile the shim into a native shared
# library, and — when a `dotnet` SDK is present — run a managed round-trip that
# P/Invokes it. Three gates of the plan's shape:
#   1. goldens (needs only the compiler)  — csharp.shim_golden / csharp.bindings_golden
#   2. native build (only the compiler)   — the csharp.build fixture
#   3. behavioral round-trip (dotnet)     — csharp.roundtrip, skipped if no SDK
# Consteval locks over the rod's classification/mangling live in
# tests/core/csharp_marshal.cpp, registered with the other compile.* checks.

set(_ws ${CMAKE_CURRENT_BINARY_DIR})

# Emit shim.cpp + Bindings.cs and build the native library.
welder_csharp_generate_bindings(welder_test_csharp
  SOURCES cpp/gen.cpp
  LIBRARY welder_test_csharp_native
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp
  OUTPUT_DIR ${_ws}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cpp/cases.hpp)

# A second pair over the SHARED retpolicy cases (tests/common/cpp — welded for
# lang::cs): the cross-backend consistency check. Behavioral-only (no goldens —
# the dedicated cases.hpp pair is the golden anchor).
welder_csharp_generate_bindings(welder_test_cs_retpolicy
  SOURCES cpp/gen_retpolicy.cpp
  LIBRARY welder_test_cs_retpolicy
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/retpolicy
  DEPENDS ${WELDER_TEST_COMMON_CPP}/retpolicy.hpp)

# Building the targets both regenerates the artifacts and compiles the shims —
# the compile IS a test (the generated splices must re-derive every member).
add_test(NAME csharp.build
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_test_csharp welder_test_cs_retpolicy
          --config $<CONFIG>)
set_tests_properties(csharp.build PROPERTIES FIXTURES_SETUP csharp_native)

# The generated artifacts must match the checked-in goldens byte-for-byte
# (ignoring line-ending style for portability) — the luacats golden pattern.
add_test(NAME csharp.shim_golden
  COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol
          ${_ws}/shim.cpp ${CMAKE_CURRENT_SOURCE_DIR}/shim.golden.cpp)
add_test(NAME csharp.bindings_golden
  COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol
          ${_ws}/Bindings.cs ${CMAKE_CURRENT_SOURCE_DIR}/Bindings.golden.cs)
set_tests_properties(csharp.shim_golden csharp.bindings_golden PROPERTIES
  FIXTURES_REQUIRED csharp_native)

# The managed round-trip, gated on a dotnet SDK (like the lua-language-server /
# uv gates: missing tool = skip with a message, the goldens still gate).
find_program(WELDER_DOTNET NAMES dotnet)
if(NOT WELDER_DOTNET)
  message(STATUS
    "welder: 'dotnet' not found; skipping the C# round-trip test "
    "(set WELDER_DOTNET to override). The bindings + native library still build.")
  return()
endif()

# The target framework of the test app (the generated bindings need net7.0+ for
# [LibraryImport]; the default tracks the SDK welder develops against).
set(WELDER_CSHARP_TFM "net10.0" CACHE STRING
  "TargetFramework for the C# round-trip test app")

# The managed test project. Written with file(GENERATE) so $<TARGET_FILE:...>
# resolves to the freshly built native library, which is copied next to the app
# exe so the default P/Invoke resolver finds it. EnableDefaultCompileItems is off
# so only the two named sources compile (Program.cs + the generated Bindings.cs).
set(_app ${_ws}/app)
file(GENERATE OUTPUT ${_app}/app.csproj CONTENT
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>${WELDER_CSHARP_TFM}</TargetFramework>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <ImplicitUsings>disable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include=\"${CMAKE_CURRENT_SOURCE_DIR}/app/Program.cs\" />
    <Compile Include=\"${_ws}/Bindings.cs\" />
    <Compile Include=\"${_ws}/retpolicy/Bindings.cs\" />
    <None Include=\"$<TARGET_FILE:welder_test_csharp>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_retpolicy>\" CopyToOutputDirectory=\"PreserveNewest\" />
  </ItemGroup>
</Project>
")

# Environment for dotnet (quiet; point at the SDK root on a Homebrew macOS install).
set(_dotnet_env DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1)
if(EXISTS "/opt/homebrew/opt/dotnet/libexec")
  list(APPEND _dotnet_env DOTNET_ROOT=/opt/homebrew/opt/dotnet/libexec)
endif()

add_test(NAME csharp.roundtrip
  COMMAND ${WELDER_DOTNET} run --project ${_app} -c Release)
set_tests_properties(csharp.roundtrip PROPERTIES
  FIXTURES_REQUIRED csharp_native
  ENVIRONMENT "${_dotnet_env}")
