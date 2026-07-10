cmake_minimum_required(VERSION 3.28)
project(welder_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The one line a downstream project needs. welder::headers pulls in welder::flags
# (the gcc-16 -freflection carrier) and the cxx_std_26 feature, so no reflection
# flags are set here — that is exactly what this test verifies.
find_package(welder REQUIRED)

add_executable(welder_smoke src/smoke.cpp)
target_link_libraries(welder_smoke PRIVATE welder::headers)