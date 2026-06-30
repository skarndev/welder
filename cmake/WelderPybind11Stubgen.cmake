# Generate Python type stubs (.pyi) for a built pybind11 extension, using
# pybind11-stubgen (https://github.com/pybind/pybind11-stubgen). The tool works by
# *importing* the freshly built module and introspecting it at runtime, so the
# interpreter passed in PYTHON must be able to import the extension (ABI match)
# *and* have pybind11-stubgen installed.
#
#   welder_pybind11_generate_stubs(<target>
#     PYTHON      <interpreter>   # required: imports the module + has stubgen
#     [MODULE     <name>]         # importable module name (default: target name)
#     [OUTPUT_DIR <dir>]          # stub root (default: <target build dir>/stubs)
#     [ARGS       <extra args>])  # extra pybind11-stubgen arguments
#
# Adds a POST_BUILD step so the stubs are (re)generated whenever the extension is
# rebuilt. pybind11-stubgen emits a flat `<module>.pyi` for a leaf module, or a
# `<module>/__init__.pyi` package tree when the module has submodules (welder's
# bound namespaces produce the latter). `--exit-code` is passed so a stub the tool
# can't represent fails the build rather than silently degrading.
function(welder_pybind11_generate_stubs target)
  cmake_parse_arguments(S "" "PYTHON;MODULE;OUTPUT_DIR" "ARGS" ${ARGN})
  if(NOT S_PYTHON)
    message(FATAL_ERROR
      "welder_pybind11_generate_stubs(${target}): PYTHON is required")
  endif()
  if(NOT S_MODULE)
    set(S_MODULE ${target})
  endif()
  if(NOT S_OUTPUT_DIR)
    set(S_OUTPUT_DIR $<TARGET_FILE_DIR:${target}>/stubs)
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E env
            "PYTHONPATH=$<TARGET_FILE_DIR:${target}>"
            ${S_PYTHON} -m pybind11_stubgen ${S_MODULE}
            --output-dir ${S_OUTPUT_DIR} --exit-code ${S_ARGS}
    VERBATIM
    COMMENT "welder: generating Python stubs for ${S_MODULE}")
endfunction()
