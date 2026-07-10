# welder is header-only: a header-only core plus header-only language rods
# (backends). Everything lives under one tree (src/welder) so user includes always
# start from <welder/...>. (An optional C++20 module wrapper was removed until the
# gcc-16 -freflection/modules bugs are resolved — see docs "Header-only for now".)

# --- welder::headers : the header-only core (source of truth) ----------------
add_library(welder_headers INTERFACE)
add_library(welder::headers ALIAS welder_headers)
# src/ is the include root, so <welder/welder.hpp>, <welder/rods/python/pybind11/rod.hpp>
# and friends all resolve from here.
target_include_directories(welder_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_compile_features(welder_headers INTERFACE cxx_std_26)
target_link_libraries(welder_headers INTERFACE welder_flags)

# --- language rods (header-only adapters over the core resolution API) --------
add_subdirectory(welder/rods)

# --- installation / packaging -------------------------------------------------
# Header-only delivery: install the header tree and export the two INTERFACE
# targets a consumer links — welder::headers and welder::flags (the -freflection
# carrier headers pulls in) — as a relocatable CMake package, plus the reusable
# build helpers so find_package(welder) also defines welder_*_add_module /
# welder_*_generate_stubs. Rods are NOT exported: welder ships core headers only,
# and a consumer brings their own backend (pybind11/nanobind/sol2/LuaBridge3).
option(WELDER_INSTALL "Generate welder's install and packaging rules" ON)
if(WELDER_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  # Export as welder::headers / welder::flags (the namespaced ALIAS names), not
  # welder::welder_headers — EXPORT_NAME renames the real target in the export set;
  # the alias itself can't be exported.
  set_target_properties(welder_headers PROPERTIES EXPORT_NAME headers)
  set_target_properties(welder_flags PROPERTIES EXPORT_NAME flags)

  # welder_flags lives in the top-level CMakeLists; both are defined by now, so a
  # single export set (with the welder:: namespace applied on install) captures the
  # welder::headers -> welder::flags link that carries -freflection to consumers.
  install(TARGETS welder_headers welder_flags
    EXPORT welder-targets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/welder
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")

  set(WELDER_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/welder)

  install(EXPORT welder-targets
    FILE welder-targets.cmake
    NAMESPACE welder::
    DESTINATION ${WELDER_INSTALL_CMAKEDIR})

  # Ship the build helpers so welder-config.cmake can include() them (see
  # cmake/welder-config.cmake.in).
  install(FILES
    ${CMAKE_SOURCE_DIR}/cmake/WelderPybind11Stubgen.cmake
    ${CMAKE_SOURCE_DIR}/cmake/WelderSol2Module.cmake
    ${CMAKE_SOURCE_DIR}/cmake/WelderLuaBridgeModule.cmake
    ${CMAKE_SOURCE_DIR}/cmake/WelderLuaCATSStub.cmake
    DESTINATION ${WELDER_INSTALL_CMAKEDIR})

  configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/welder-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/welder-config.cmake
    INSTALL_DESTINATION ${WELDER_INSTALL_CMAKEDIR})

  # Header-only + arch-independent: SameMajorVersion compatibility, no arch check.
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/welder-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/welder-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/welder-config-version.cmake
    DESTINATION ${WELDER_INSTALL_CMAKEDIR})
endif()
