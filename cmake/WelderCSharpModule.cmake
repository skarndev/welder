# welder_csharp_generate_bindings(<name>
#   SOURCES      <gen.cpp>...   # required: the generator TU(s), one using
#                               #           WELDER_CSHARP_MAIN(<ns>, <header>, <lib>)
#   LIBRARY      <libname>      # required: the P/Invoke library base name — MUST match
#                               #           the `lib` argument of WELDER_CSHARP_MAIN
#   [OUTPUT_DIR  <dir>]         # where shim.cpp + Bindings.cs land (default: binary dir)
#   [INCLUDE_DIRS <dir>...]     # extra include dirs (where the welded header lives)
#   [LINK        <targets>...]  # extra link targets whose headers the shim/gen need
#   [DEPENDS     <files>...])   # extra dependencies that retrigger generation
#
# The C#/.NET analogue of welder_luacats_generate_stub / welder_sol2_add_module. C#
# has no in-process class-registration C API, so welder binds it over a C ABI driven
# by P/Invoke — which means TWO generated artifacts from one reflection pass: an
# `extern "C"` shim (compiled into a shared library) and a `[LibraryImport]` C#
# wrapper. This helper:
#
#   1. builds a generator executable from SOURCES (linked against welder::csharp),
#   2. runs it to emit  <OUTPUT_DIR>/shim.cpp  and  <OUTPUT_DIR>/Bindings.cs,
#   3. compiles the shim into a SHARED library target `<name>`, named
#      lib<LIBRARY>.{so,dylib} / <LIBRARY>.dll so the default P/Invoke resolver finds
#      it from the managed app's base directory.
#
# The generated C# wrapper path is stored in `<name>`'s WELDER_CSHARP_BINDINGS
# property; a consumer compiles it into a .NET project (see tests/csharp) with the
# shared library alongside. The shim #includes the welded header (welder
# annotations) AND re-runs the generator's reflection queries (the splice layer in
# <welder/rods/csharp/shim_support.hpp>), so it is built with the reflection flag +
# the welded types' includes — both flow in via welder::csharp and
# INCLUDE_DIRS/LINK. Module scanning is kept OFF to match the other reflection TUs
# (the gcc-16 header-unit macro-visibility issue).
#
# Windows/MinGW: the library is emitted UNPREFIXED (<LIBRARY>.dll, not
# lib<LIBRARY>.dll) so .NET's default DllImport resolver finds it, and the gcc
# runtimes are linked statically so the dll loads under a native (non-MSYS)
# dotnet without libstdc++-6.dll/libgcc on PATH.
function(welder_csharp_generate_bindings name)
  cmake_parse_arguments(CS "" "LIBRARY;OUTPUT_DIR"
    "SOURCES;INCLUDE_DIRS;LINK;DEPENDS" ${ARGN})
  if(NOT CS_SOURCES)
    message(FATAL_ERROR "welder_csharp_generate_bindings(${name}): SOURCES is required")
  endif()
  if(NOT CS_LIBRARY)
    message(FATAL_ERROR "welder_csharp_generate_bindings(${name}): LIBRARY is required")
  endif()
  if(NOT CS_OUTPUT_DIR)
    set(CS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  set(_shim ${CS_OUTPUT_DIR}/shim.cpp)
  set(_cs ${CS_OUTPUT_DIR}/Bindings.cs)

  # 1) the generator executable
  set(_gen ${name}_gen)
  add_executable(${_gen} ${CS_SOURCES})
  target_link_libraries(${_gen} PRIVATE welder::csharp ${CS_LINK})
  # welder's own build applies its strict warning set; a consumer using this
  # helper won't have the target, so the link is skipped for them.
  if(TARGET welder_warnings)
    target_link_libraries(${_gen} PRIVATE welder_warnings)
  endif()
  target_include_directories(${_gen} PRIVATE ${CS_INCLUDE_DIRS})
  target_compile_features(${_gen} PRIVATE cxx_std_26)
  set_target_properties(${_gen} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

  # 2) run it -> shim.cpp + Bindings.cs
  add_custom_command(
    OUTPUT ${_shim} ${_cs}
    COMMAND ${_gen} ${_shim} ${_cs}
    DEPENDS ${_gen} ${CS_DEPENDS}
    VERBATIM
    COMMENT "welder: generating C#/.NET bindings for ${name}")

  # 3) the native shared library from the generated shim (NOT welder_warnings:
  # the generated file is machine-written; wire-width conversions are implicit
  # by design there)
  add_library(${name} SHARED ${_shim})
  target_link_libraries(${name} PRIVATE welder::csharp ${CS_LINK})
  target_include_directories(${name} PRIVATE ${CS_INCLUDE_DIRS})
  target_compile_features(${name} PRIVATE cxx_std_26)
  set_target_properties(${name} PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
    OUTPUT_NAME ${CS_LIBRARY}
    WELDER_CSHARP_BINDINGS ${_cs})
  if(WIN32)
    # .NET's resolver probes <LIBRARY>.dll; MinGW would emit lib<LIBRARY>.dll.
    set_target_properties(${name} PROPERTIES PREFIX "")
    if(MINGW)
      target_link_options(${name} PRIVATE
        -static-libstdc++ -static-libgcc)
    endif()
  endif()
endfunction()

# welder_csharp_nuget_project(<name>              # a welder_csharp_generate_bindings target
#   PACKAGE_ID   <Acme.Geo>                       # required: the NuGet package id
#   [VERSION     <1.2.3>]                         # default: 0.1.0
#   [TFM         <net8.0>]                        # default: net8.0 ([LibraryImport] needs net7+)
#   [OUTPUT_DIR  <dir>])                          # default: <binary dir>/<PACKAGE_ID>
#
# The .NET distribution half: writes a PACKABLE SDK-style .csproj around the
# generated bindings, so
#
#   dotnet pack <OUTPUT_DIR>/<PACKAGE_ID>.csproj -c Release
#
# yields a standard NuGet package — the managed assembly under lib/<TFM>/ and the
# native shim library under runtimes/<rid>/native/ (the layout NuGet's runtime
# probing expects; the .NET host loads the right native library automatically).
# The RID is derived from the CMake target platform, so the package carries THIS
# build's native library; a multi-platform (fat) package is produced by packing on
# each platform and merging the runtimes/ trees — or by a CI matrix publishing
# per-RID packages (document either in your release pipeline).
#
# The csproj is written with file(GENERATE), so $<TARGET_FILE:...> resolves to the
# built shim; `dotnet pack` must run AFTER the native target is built.
function(welder_csharp_nuget_project name)
  cmake_parse_arguments(NG "" "PACKAGE_ID;VERSION;TFM;OUTPUT_DIR" "" ${ARGN})
  if(NOT NG_PACKAGE_ID)
    message(FATAL_ERROR "welder_csharp_nuget_project(${name}): PACKAGE_ID is required")
  endif()
  if(NOT NG_VERSION)
    set(NG_VERSION 0.1.0)
  endif()
  if(NOT NG_TFM)
    set(NG_TFM net8.0)
  endif()
  if(NOT NG_OUTPUT_DIR)
    set(NG_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/${NG_PACKAGE_ID})
  endif()

  # The NuGet runtime identifier of the CMake target platform.
  if(WIN32)
    set(_os win)
  elseif(APPLE)
    set(_os osx)
  else()
    set(_os linux)
  endif()
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(_arch arm64)
  else()
    set(_arch x64)
  endif()
  set(_rid ${_os}-${_arch})

  get_target_property(_bindings ${name} WELDER_CSHARP_BINDINGS)
  file(GENERATE OUTPUT ${NG_OUTPUT_DIR}/${NG_PACKAGE_ID}.csproj CONTENT
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <TargetFramework>${NG_TFM}</TargetFramework>
    <PackageId>${NG_PACKAGE_ID}</PackageId>
    <Version>${NG_VERSION}</Version>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <ImplicitUsings>disable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AppendTargetFrameworkToOutputPath>false</AppendTargetFrameworkToOutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include=\"${_bindings}\" />
    <None Include=\"$<TARGET_FILE:${name}>\" Pack=\"true\"
          PackagePath=\"runtimes/${_rid}/native/\"
          CopyToOutputDirectory=\"PreserveNewest\" />
  </ItemGroup>
</Project>
")
endfunction()
