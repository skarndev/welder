# welder_generate_opaque_containers(<name>
#   SOURCES  <gen.cpp>...   # required: the generator TU(s), one using
#                           #           WELDER_OPAQUE_CONTAINERS_MAIN(<namespace>)
#   [OUTPUT  <file>]        # header path (default: <binary dir>/<name>.opaque.hpp)
#   [DEPENDS <files>...])   # extra dependencies that should retrigger generation
#
# Generate a header of WELDER_OPAQUE(...) declarations + welded aliases that bind the
# STL containers a welded namespace uses OPAQUELY (by reference: live mutation,
# append, zero-copy numpy) instead of the default by-value copy — so the user need
# not hand-write that per-container boilerplate. The generated header is
# backend-neutral (it uses welder's neutral WELDER_OPAQUE macro), so one header serves
# both Python backends; the consuming TU includes the active backend's
# <welder/rods/python/<backend>/rod.hpp> first (which defines WELDER_OPAQUE), then this
# header, before welding the namespace.
#
# Like the trampolines (welder_generate_trampolines) and LuaCATS stub, the opaque
# declarations cannot be produced by a runtime rod (a namespace-scope type_caster
# specialization is a compile-time artifact) — so they are reflection-emitted at build
# time: this builds a tiny generator executable from SOURCES (linked against
# welder::opaque_containers, no pybind11/nanobind), runs it, and captures its output.
#
#   welder_generate_opaque_containers(app_opaque
#     SOURCES gen.cpp        # #include the welded headers + WELDER_OPAQUE_CONTAINERS_MAIN(app)
#     OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/app.opaque.hpp)
#
# Creates an ALL target <name> that (re)builds the header when the generator or its
# DEPENDS change; the produced path is stored in the target's
# WELDER_OPAQUE_CONTAINERS_HEADER property. The generator writes the file directly
# (WELDER_OPAQUE_CONTAINERS_MAIN takes the output path as argv[1]), so no shell
# redirection is involved.
function(welder_generate_opaque_containers name)
  cmake_parse_arguments(O "" "OUTPUT" "SOURCES;DEPENDS" ${ARGN})
  if(NOT O_SOURCES)
    message(FATAL_ERROR
      "welder_generate_opaque_containers(${name}): SOURCES is required")
  endif()
  if(NOT O_OUTPUT)
    set(O_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.opaque.hpp)
  endif()

  set(_gen ${name}_gen)
  add_executable(${_gen} ${O_SOURCES})
  target_link_libraries(${_gen} PRIVATE welder::opaque_containers)
  # welder's own build applies its strict warning set; a consumer using this helper
  # won't have the target, so the link is skipped for them.
  if(TARGET welder_warnings)
    target_link_libraries(${_gen} PRIVATE welder_warnings)
  endif()
  target_compile_features(${_gen} PRIVATE cxx_std_26)

  add_custom_command(
    OUTPUT ${O_OUTPUT}
    COMMAND ${_gen} ${O_OUTPUT}
    DEPENDS ${_gen} ${O_DEPENDS}
    VERBATIM
    COMMENT "welder: generating opaque containers ${O_OUTPUT}")
  add_custom_target(${name} ALL DEPENDS ${O_OUTPUT})
  set_property(TARGET ${name} PROPERTY WELDER_OPAQUE_CONTAINERS_HEADER ${O_OUTPUT})
endfunction()
