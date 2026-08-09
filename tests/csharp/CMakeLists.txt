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

welder_csharp_generate_bindings(welder_test_cs_operators
  SOURCES cpp/gen_operators.cpp
  LIBRARY welder_test_cs_operators
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/operators
  DEPENDS ${WELDER_TEST_COMMON_CPP}/operators.hpp)

welder_csharp_generate_bindings(welder_test_cs_inheritance
  SOURCES cpp/gen_inheritance.cpp
  LIBRARY welder_test_cs_inheritance
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/inheritance
  DEPENDS ${WELDER_TEST_COMMON_CPP}/inheritance.hpp)

welder_csharp_generate_bindings(welder_test_cs_overridable
  SOURCES cpp/gen_overridable.cpp
  LIBRARY welder_test_cs_overridable
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/overridable
  DEPENDS ${WELDER_TEST_COMMON_CPP}/overridable.hpp)

welder_csharp_generate_bindings(welder_test_cs_resolution
  SOURCES cpp/gen_resolution.cpp
  LIBRARY welder_test_cs_resolution
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/resolution
  DEPENDS ${WELDER_TEST_COMMON_CPP}/resolution.hpp)

welder_csharp_generate_bindings(welder_test_cs_templates
  SOURCES cpp/gen_templates.cpp
  LIBRARY welder_test_cs_templates
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/templates
  DEPENDS ${WELDER_TEST_COMMON_CPP}/templates.hpp)

welder_csharp_generate_bindings(welder_test_cs_copying
  SOURCES cpp/gen_copying.cpp
  LIBRARY welder_test_cs_copying
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/cpp ${WELDER_TEST_COMMON_CPP}
  OUTPUT_DIR ${_ws}/copying
  DEPENDS ${WELDER_TEST_COMMON_CPP}/copying.hpp)

# Building the targets both regenerates the artifacts and compiles the shims —
# the compile IS a test (the generated splices must re-derive every member).
add_test(NAME csharp.build
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
          --target welder_test_csharp welder_test_cs_retpolicy welder_test_cs_operators
                  welder_test_cs_inheritance welder_test_cs_overridable
                  welder_test_cs_resolution welder_test_cs_templates
                  welder_test_cs_copying
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

# The managed test project — an xUnit test assembly run via `dotnet test`
# (restores xunit from nuget.org on first configure-build). Written with
# file(GENERATE) so $<TARGET_FILE:...> resolves to the freshly built native
# libraries, copied next to the test assembly so the default P/Invoke resolver
# finds them. EnableDefaultCompileItems is off so only the named sources
# compile (BindingTests.cs + the generated Bindings.cs files).
set(_app ${_ws}/app)
file(GENERATE OUTPUT ${_app}/app.csproj CONTENT
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <TargetFramework>${WELDER_CSHARP_TFM}</TargetFramework>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <ImplicitUsings>disable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <IsPackable>false</IsPackable>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include=\"Microsoft.NET.Test.Sdk\" Version=\"17.11.1\" />
    <PackageReference Include=\"xunit\" Version=\"2.9.2\" />
    <PackageReference Include=\"xunit.runner.visualstudio\" Version=\"2.8.2\" />
  </ItemGroup>
  <ItemGroup>
    <Compile Include=\"${CMAKE_CURRENT_SOURCE_DIR}/app/BindingTests.cs\" />
    <Compile Include=\"${_ws}/Bindings.cs\" />
    <Compile Include=\"${_ws}/retpolicy/Bindings.cs\" />
    <Compile Include=\"${_ws}/operators/Bindings.cs\" />
    <Compile Include=\"${_ws}/inheritance/Bindings.cs\" />
    <Compile Include=\"${_ws}/overridable/Bindings.cs\" />
    <Compile Include=\"${_ws}/resolution/Bindings.cs\" />
    <Compile Include=\"${_ws}/templates/Bindings.cs\" />
    <Compile Include=\"${_ws}/copying/Bindings.cs\" />
    <None Include=\"$<TARGET_FILE:welder_test_csharp>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_retpolicy>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_operators>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_inheritance>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_overridable>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_resolution>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_templates>\" CopyToOutputDirectory=\"PreserveNewest\" />
    <None Include=\"$<TARGET_FILE:welder_test_cs_copying>\" CopyToOutputDirectory=\"PreserveNewest\" />
  </ItemGroup>
</Project>
")

# Environment for dotnet (quiet; a Homebrew install needs DOTNET_ROOT pointing
# at the real SDK root — resolve it from the found executable so macOS and
# linuxbrew both work).
set(_dotnet_env DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1)
get_filename_component(_dotnet_real ${WELDER_DOTNET} REALPATH)
get_filename_component(_dotnet_root ${_dotnet_real} DIRECTORY)
if(EXISTS "${_dotnet_root}/sdk")
  list(APPEND _dotnet_env DOTNET_ROOT=${_dotnet_root})
endif()

add_test(NAME csharp.roundtrip
  COMMAND ${WELDER_DOTNET} test ${_app} -c Release)
set_tests_properties(csharp.roundtrip PROPERTIES
  FIXTURES_REQUIRED csharp_native
  ENVIRONMENT "${_dotnet_env}")
