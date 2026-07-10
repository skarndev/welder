# welder's usage requirements, *checked* on a consumer rather than imposed on their
# targets. Included both by welder's own build (the top-level CMakeLists) and by the
# installed welder-config.cmake, so find_package(welder) and
# add_subdirectory()/FetchContent() consumers get the same early diagnostics.
#
# welder does NOT force the C++ standard onto a consumer's target (there is no
# INTERFACE cxx_std_26 on welder::headers) — the language level is the consumer's
# project-wide dial. So this checks it instead and fails with a clear message. The
# in-header guard in <welder/lang.hpp> (keyed on __cpp_impl_reflection) is the
# backstop for what CMake can't see here: a per-target standard, an unset standard
# (compilers default below C++26), or a missing -freflection.
function(welder_check_requirements)
  # Compiler: gcc-16 is the only one implementing P2996 + P3394 today. Add branches
  # as Clang/MSVC catch up.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
      message(FATAL_ERROR
        "welder needs GCC >= 16 for C++26 reflection (got ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
  else()
    message(WARNING
      "welder: C++26 reflection is only wired for GNU (gcc-16). Compiler "
      "'${CMAKE_CXX_COMPILER_ID}' is not supported yet; the build will almost "
      "certainly fail until P2996/P3394 land there.")
  endif()

  # Language standard: required, not imposed. We can only see the project-wide
  # CMAKE_CXX_STANDARD from here, so catch an explicitly-too-low pin early; the header
  # guard covers the rest.
  if(DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS 26)
    message(FATAL_ERROR
      "welder requires C++26, but CMAKE_CXX_STANDARD is ${CMAKE_CXX_STANDARD}. Set it "
      "to 26 (or newer) on your target or project — welder does not raise it for you.")
  endif()
endfunction()