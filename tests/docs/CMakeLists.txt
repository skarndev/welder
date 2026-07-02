# C++ docs pipeline (core-only: no Python, no pybind11 — needs just the
# compiler, so it registers regardless of the uv-env gate). The generator
# reflects over tests/docs/corpus.hpp and emits its Doxygen shadow header; the
# golden test locks the exact output. expected_atelier.hpp is committed and
# hand-reviewed — regenerate after an intentional change with:
#   <build>/tests/docs/welder_docs_gen tests/docs/expected_atelier.hpp
add_executable(welder_docs_gen gen.cpp)
target_compile_features(welder_docs_gen PRIVATE cxx_std_26)
target_link_libraries(welder_docs_gen PRIVATE welder::headers)

add_test(NAME docs.generate
  COMMAND welder_docs_gen ${CMAKE_CURRENT_BINARY_DIR}/atelier.hpp)
set_tests_properties(docs.generate PROPERTIES FIXTURES_SETUP welder_docs_shadow)

add_test(NAME docs.golden
  COMMAND ${CMAKE_COMMAND} -E compare_files
          ${CMAKE_CURRENT_BINARY_DIR}/atelier.hpp
          ${CMAKE_CURRENT_SOURCE_DIR}/expected_atelier.hpp)
set_tests_properties(docs.golden PROPERTIES FIXTURES_REQUIRED welder_docs_shadow)
