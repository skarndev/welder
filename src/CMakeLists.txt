# welder is header-only at its core, with an optional single importable module
# and header-only language backends.

# --- welder::headers : the header-only core (source of truth) ----------------
add_library(welder_headers INTERFACE)
add_library(welder::headers ALIAS welder_headers)
target_include_directories(welder_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(welder_headers INTERFACE cxx_std_26)
target_link_libraries(welder_headers INTERFACE welder_flags)

# --- welder::module : optional `import welder;` wrapper (exports vocabulary) --
if(WELDER_BUILD_MODULE)
  add_library(welder_module STATIC)
  add_library(welder::module ALIAS welder_module)
  target_sources(welder_module
    PUBLIC
      FILE_SET welder_module_ifc TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_SOURCE_DIR}/modules
        FILES ${CMAKE_SOURCE_DIR}/modules/welder.cppm)
  target_compile_features(welder_module PUBLIC cxx_std_26)
  # PUBLIC so importers also get the include path (for backend headers) + flags.
  target_link_libraries(welder_module PUBLIC welder_headers welder_flags)
endif()

# --- welder::python : pybind11 backend (header-only adapter) -----------------
if(WELDER_BUILD_PYTHON)
  find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
  find_package(pybind11 REQUIRED)

  add_library(welder_python INTERFACE)
  add_library(welder::python ALIAS welder_python)
  target_link_libraries(welder_python INTERFACE
    welder_headers pybind11::headers Python::Module)
endif()
