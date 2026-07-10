cmake_minimum_required(VERSION 3.28)
project(welder_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# welder checks its requirements at find_package() time (compiler + C++ standard) but
# imposes nothing on this target: the consumer owns the standard (C++26, set above)
# and gcc's -freflection. This is exactly what the test proves — welder::headers is
# just the include path.
find_package(welder REQUIRED)

add_executable(welder_smoke src/smoke.cpp)
target_link_libraries(welder_smoke PRIVATE welder::headers)
target_compile_options(welder_smoke PRIVATE -freflection)  # welder does not add this for you