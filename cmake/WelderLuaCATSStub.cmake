# welder_luacats_generate_stub(<name>
#   SOURCES  <gen.cpp>...   # required: the generator TU(s), one using
#                           #           WELDER_LUACATS_MAIN(<namespace>)
#   [OUTPUT  <file>]        # stub path (default: <binary dir>/<name>.lua)
#   [DEPENDS <files>...])   # extra dependencies that should retrigger the stub
#
# Generate a LuaCATS (`---@meta`) definition file for welder-bound Lua types — the
# Lua analogue of welder_pybind11_generate_stubs. Unlike the Python `.pyi` path
# (pybind11-stubgen *imports* the built module and introspects it), a Lua stub
# cannot be scraped from a loaded module, so it is reflection-emitted at build
# time: this builds a tiny generator executable from SOURCES (linked against
# welder::luacats), runs it, and captures its output to the stub file.
#
#   welder_luacats_generate_stub(geometry_luacats
#     SOURCES stub_gen.cpp          # #include the bound headers + WELDER_LUACATS_MAIN(geometry)
#     OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/geometry.lua)
#
# Creates an ALL target <name> that (re)builds the stub when the generator or its
# DEPENDS change; the produced path is stored in the target's WELDER_LUACATS_STUB
# property. The generator writes the file directly (WELDER_LUACATS_MAIN takes the
# output path as argv[1]), so no shell redirection is involved.
function(welder_luacats_generate_stub name)
  cmake_parse_arguments(S "" "OUTPUT" "SOURCES;DEPENDS" ${ARGN})
  if(NOT S_SOURCES)
    message(FATAL_ERROR
      "welder_luacats_generate_stub(${name}): SOURCES is required")
  endif()
  if(NOT S_OUTPUT)
    set(S_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}.lua)
  endif()

  set(_gen ${name}_gen)
  add_executable(${_gen} ${S_SOURCES})
  target_link_libraries(${_gen} PRIVATE welder::luacats)
  target_compile_features(${_gen} PRIVATE cxx_std_26)
  # The generator is header-only welder and pulls in no Lua headers, but keep
  # module scanning off to match the other Lua-side TUs and stay clear of the
  # gcc-16 header-unit macro-visibility issues (see WelderSol2Module).
  set_target_properties(${_gen} PROPERTIES CXX_SCAN_FOR_MODULES OFF)

  add_custom_command(
    OUTPUT ${S_OUTPUT}
    COMMAND ${_gen} ${S_OUTPUT}
    DEPENDS ${_gen} ${S_DEPENDS}
    VERBATIM
    COMMENT "welder: generating LuaCATS stub ${S_OUTPUT}")
  add_custom_target(${name} ALL DEPENDS ${S_OUTPUT})
  set_property(TARGET ${name} PROPERTY WELDER_LUACATS_STUB ${S_OUTPUT})
endfunction()
