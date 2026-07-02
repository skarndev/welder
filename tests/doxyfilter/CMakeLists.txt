# Doxygen INPUT_FILTER tests. The filter is pure text -> text, so the golden
# test needs only Python 3; the end-to-end test additionally needs doxygen on
# PATH (it checks the translated comments actually *attach* — placement is the
# fragile part) and self-skips when absent. corpus.hpp is never compiled.
find_program(WELDER_PYTHON3 python3)
if(NOT WELDER_PYTHON3)
  message(WARNING "welder: python3 not found; doxyfilter tests skipped.")
  return()
endif()

set(_filter ${CMAKE_SOURCE_DIR}/tools/welder-doxygen-filter.py)

# The golden: filtered corpus must match the committed, hand-reviewed
# expected_corpus.hpp byte for byte. Regenerate after an intentional change:
#   python3 tools/welder-doxygen-filter.py tests/doxyfilter/corpus.hpp \
#     > tests/doxyfilter/expected_corpus.hpp
add_test(NAME doxyfilter.golden
  COMMAND ${CMAKE_COMMAND}
    -DPY=${WELDER_PYTHON3} -DFILTER=${_filter}
    -DIN=${CMAKE_CURRENT_SOURCE_DIR}/corpus.hpp
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/expected_corpus.hpp
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/filtered_corpus.hpp
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_and_compare.cmake)

find_program(WELDER_DOXYGEN doxygen)
if(WELDER_DOXYGEN)
  add_test(NAME doxyfilter.doxygen
    COMMAND ${WELDER_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/doxygen_e2e.py
            ${WELDER_DOXYGEN} ${_filter}
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus.hpp
            ${CMAKE_CURRENT_BINARY_DIR}/e2e)
else()
  message(STATUS "welder: doxygen not found; doxyfilter.doxygen e2e test skipped.")
endif()
