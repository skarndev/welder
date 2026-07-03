# Run the welder Doxygen filter over IN and compare with the golden EXPECTED.
# Driven by the doxyfilter.golden CTest; variables arrive via -D.
execute_process(
  COMMAND ${PY} ${FILTER} ${IN}
  OUTPUT_FILE ${OUT}
  RESULT_VARIABLE _run)
if(NOT _run EQUAL 0)
  message(FATAL_ERROR "welder_doxygen_filter failed (${_run}) on ${IN}")
endif()
execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files ${OUT} ${EXPECTED}
  RESULT_VARIABLE _cmp)
if(NOT _cmp EQUAL 0)
  message(FATAL_ERROR "filtered output differs from golden:\n  ${OUT}\n  ${EXPECTED}")
endif()
