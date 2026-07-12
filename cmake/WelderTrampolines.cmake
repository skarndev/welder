# welder_generate_trampolines(<name>
#   SOURCES  <gen.cpp>...   # required: the generator TU(s), one using
#                           #           WELDER_TRAMPOLINES_MAIN(<namespace>)
#   [OUTPUT  <file>]        # header path (default: <binary dir>/<name>.trampolines.hpp)
#   [DEPENDS <files>...])   # extra dependencies that should retrigger generation
#
# Generate a header of ready-to-compile pybind11/nanobind trampoline subclasses for
# the welder-bound virtual types in a namespace — so a Python subclass can override
# their virtuals without the trampolines being hand-written. The generated header is
# backend-neutral (it uses welder's neutral WELDER_PY_TRAMPOLINE / WELDER_PY_OVERRIDE
# macros), so one header serves both Python backends; the consuming TU includes the
# active backend's <welder/rods/python/<backend>/trampoline.hpp> first, then this
# header, before binding the namespace.
#
# Like the LuaCATS stub (welder_luacats_generate_stub) and unlike the Python `.pyi`
# path, the trampolines cannot be scraped from a loaded module — they are the source
# the module is built FROM — so they are reflection-emitted at build time: this builds
# a tiny generator executable from SOURCES (linked against welder::trampolines, no
# pybind11/nanobind), runs it, and captures its output to the header.
#
#   welder_generate_trampolines(zoo_trampolines
#     SOURCES gen.cpp        # #include the welded headers + WELDER_TRAMPOLINES_MAIN(zoo)
#     OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/zoo.trampolines.hpp)
#
# Creates an ALL target <name> that (re)builds the header when the generator or its
# DEPENDS change; the produced path is stored in the target's WELDER_TRAMPOLINES_HEADER
# property. The generator writes the file directly (WELDER_TRAMPOLINES_MAIN takes the
# output path as argv[1]), so no shell redirection is involved.
function(welder_generate_trampolines name)
  cmake_parse_arguments(T "" "OUTPUT" "SOURCES;DEPENDS" ${ARGN})
  if(NOT T_SOURCES)
    message(FATAL_ERROR
      "welder_generate_trampolines(${name}): SOURCES is required")
  endif()
  if(NOT T_OUTPUT)
    set(T_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.trampolines.hpp)
  endif()

  set(_gen ${name}_gen)
  add_executable(${_gen} ${T_SOURCES})
  target_link_libraries(${_gen} PRIVATE welder::trampolines)
  # welder's own build applies its strict warning set; a consumer using this helper
  # won't have the target, so the link is skipped for them.
  if(TARGET welder_warnings)
    target_link_libraries(${_gen} PRIVATE welder_warnings)
  endif()
  target_compile_features(${_gen} PRIVATE cxx_std_26)

  add_custom_command(
    OUTPUT ${T_OUTPUT}
    COMMAND ${_gen} ${T_OUTPUT}
    DEPENDS ${_gen} ${T_DEPENDS}
    VERBATIM
    COMMENT "welder: generating trampolines ${T_OUTPUT}")
  add_custom_target(${name} ALL DEPENDS ${T_OUTPUT})
  set_property(TARGET ${name} PROPERTY WELDER_TRAMPOLINES_HEADER ${T_OUTPUT})
endfunction()