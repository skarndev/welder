# Doxygen INPUT_FILTER tests. The filter is pure text -> text but needs a
# Python with `lark` (its grammar engine): prefer the uv test venv (which pins
# it), else a python3 that can already import lark, else skip. The golden
# tests need only that interpreter; the end-to-end test additionally needs
# doxygen on PATH (it checks the translated comments actually *attach* —
# placement is the fragile part) and self-skips when absent. corpus.hpp and
# hostile.hpp are never compiled.
set(WELDER_FILTER_PYTHON "")
if(WELDER_TEST_VENV_PYTHON)
  set(WELDER_FILTER_PYTHON ${WELDER_TEST_VENV_PYTHON})
else()
  find_program(WELDER_PYTHON3 python3)
  if(WELDER_PYTHON3)
    execute_process(COMMAND ${WELDER_PYTHON3} -c "import lark"
      RESULT_VARIABLE _lark_res OUTPUT_QUIET ERROR_QUIET)
    if(_lark_res EQUAL 0)
      set(WELDER_FILTER_PYTHON ${WELDER_PYTHON3})
    endif()
  endif()
endif()
if(NOT WELDER_FILTER_PYTHON)
  message(WARNING "welder: no Python with 'lark' found; doxyfilter tests skipped.")
  return()
endif()

set(_filter ${CMAKE_SOURCE_DIR}/tools/welder_doxygen_filter.py)

# The goldens: filtered input must match the committed, hand-reviewed
# expected_*.hpp byte for byte. corpus.hpp locks every comment placement;
# hostile.hpp locks the fail-safety contract (broken C++, unterminated
# literals/blocks, non-UTF-8 bytes -> exit 0, deterministic output, code
# never swallowed). Regenerate after an intentional change:
#   python3 tools/welder_doxygen_filter.py tests/doxyfilter/corpus.hpp \
#     > tests/doxyfilter/expected_corpus.hpp    (same for hostile)
foreach(case corpus hostile)
  add_test(NAME doxyfilter.golden.${case}
    COMMAND ${CMAKE_COMMAND}
      -DPY=${WELDER_FILTER_PYTHON} -DFILTER=${_filter}
      -DIN=${CMAKE_CURRENT_SOURCE_DIR}/${case}.hpp
      -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected_${case}.hpp
      -DOUT=${CMAKE_CURRENT_BINARY_DIR}/filtered_${case}.hpp
      -P ${CMAKE_CURRENT_SOURCE_DIR}/run_and_compare.cmake)
endforeach()

find_program(WELDER_DOXYGEN doxygen)
if(NOT WELDER_DOXYGEN)
  message(STATUS "welder: doxygen not found; doxyfilter e2e test + HTML docs skipped.")
  return()
endif()

add_test(NAME doxyfilter.doxygen
  COMMAND ${WELDER_FILTER_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/doxygen_e2e.py
          ${WELDER_DOXYGEN} ${_filter}
          ${CMAKE_CURRENT_SOURCE_DIR}/corpus.hpp
          ${CMAKE_CURRENT_BINARY_DIR}/e2e)

# Browsable HTML of the filtered corpus, to eyeball what Doxygen makes of the
# translated comments: open
#   ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
# Part of ALL (rebuilds only when corpus/filter/grammar change); also
# available as `cmake --build ... --target doxyfilter-html`.
configure_file(Doxyfile.html.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.html @ONLY)
add_custom_command(
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
  COMMAND ${WELDER_DOXYGEN} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.html
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/corpus.hpp ${_filter}
          ${CMAKE_SOURCE_DIR}/tools/welder_doxygen_filter.lark
          ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.html
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "welder: Doxygen HTML for the doxyfilter corpus -> tests/doxyfilter/html/")
add_custom_target(doxyfilter-html ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/html/index.html)
