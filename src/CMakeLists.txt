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
# welder::headers carries ONLY the include path. It deliberately does not force the
# C++ standard (no cxx_std_26) or gcc's -freflection onto a consumer's target — those
# are the consumer's to set, checked (WelderRequirements + the <welder/lang.hpp> guard)
# not imposed. welder's own in-tree targets get -freflection from a directory-scoped
# add_compile_options in the top-level CMakeLists, which does not reach consumers.

# --- language rods (header-only adapters over the core resolution API) --------
add_subdirectory(welder/rods)

# --- installation / packaging -------------------------------------------------
# Header-only delivery: install the header tree and export the one INTERFACE target
# a consumer links — welder::headers (the include path; it imposes neither the C++
# standard nor -freflection, which the consumer sets and WelderRequirements + the
# <welder/lang.hpp> guard check) — as a relocatable CMake package, plus the reusable
# build helpers so find_package(welder) also defines welder_*_add_module /
# welder_*_generate_stubs and runs welder_check_requirements(). Rods are NOT exported:
# welder ships core headers only, and a consumer brings their own backend
# (pybind11/nanobind/sol2/LuaBridge3).
# Default ON only at top level (developing/packaging welder). A consumer pulling
# welder in via add_subdirectory()/FetchContent() gets it OFF, so welder's install
# rules never leak into their `cmake --install` — they just link welder::headers.
option(WELDER_INSTALL "Generate welder's install and packaging rules" ${PROJECT_IS_TOP_LEVEL})
if(WELDER_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  # Export as welder::headers (the namespaced ALIAS name), not welder::welder_headers
  # — EXPORT_NAME renames the real target in the export set; the alias itself can't be
  # exported. Only the include-path target is exported; welder ships no flags target
  # (the standard + -freflection are the consumer's, checked not propagated).
  set_target_properties(welder_headers PROPERTIES EXPORT_NAME headers)

  install(TARGETS welder_headers
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
    ${CMAKE_SOURCE_DIR}/cmake/WelderRequirements.cmake
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
