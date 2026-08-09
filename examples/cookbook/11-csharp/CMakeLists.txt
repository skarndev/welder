# Cookbook 11 — C#/.NET bindings: welder_csharp_generate_bindings() builds the
# generator (gen.cpp), runs it into shim.cpp + Bindings.cs, and compiles the shim
# into the native library; the check app compiles Bindings.cs and P/Invokes it.
# welder_csharp_nuget_project() then wraps the same pair into a packable csproj —
# `dotnet pack` yields a standard NuGet package (assembly under lib/<tfm>/, the
# native library under runtimes/<rid>/native/).
# See docs/content/cookbook/csharp.md.
welder_csharp_generate_bindings(inventory_cs
  SOURCES gen.cpp
  LIBRARY cookbook_inventory
  INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/inventory.hpp)

welder_csharp_nuget_project(inventory_cs
  PACKAGE_ID Cookbook.Inventory
  VERSION 0.1.0
  TFM ${WELDER_COOKBOOK_CSHARP_TFM})

# The console check app: no package dependencies, so it builds offline with just
# the SDK. file(GENERATE) so $<TARGET_FILE:inventory_cs> resolves to the built
# native library, copied beside the app for the default P/Invoke resolver.
set(_app ${CMAKE_CURRENT_BINARY_DIR}/app)
file(GENERATE OUTPUT ${_app}/check.csproj CONTENT
"<Project Sdk=\"Microsoft.NET.Sdk\">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>${WELDER_COOKBOOK_CSHARP_TFM}</TargetFramework>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <ImplicitUsings>disable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include=\"${CMAKE_CURRENT_SOURCE_DIR}/Check.cs\" />
    <Compile Include=\"${CMAKE_CURRENT_BINARY_DIR}/Bindings.cs\" />
    <None Include=\"$<TARGET_FILE:inventory_cs>\" CopyToOutputDirectory=\"PreserveNewest\" />
  </ItemGroup>
</Project>
")

add_test(NAME cookbook.11-csharp
  COMMAND ${WELDER_COOKBOOK_DOTNET} run --project ${_app} -c Release)
add_test(NAME cookbook.11-csharp-pack
  COMMAND ${WELDER_COOKBOOK_DOTNET} pack
          ${CMAKE_CURRENT_BINARY_DIR}/Cookbook.Inventory/Cookbook.Inventory.csproj
          -c Release -o ${CMAKE_CURRENT_BINARY_DIR}/nupkg)
set_tests_properties(cookbook.11-csharp cookbook.11-csharp-pack PROPERTIES
  ENVIRONMENT "DOTNET_CLI_TELEMETRY_OPTOUT=1;DOTNET_NOLOGO=1")
