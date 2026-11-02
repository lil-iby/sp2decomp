cmake_minimum_required(VERSION 3.20)
project(sp2decomp LANGUAGES CXX)

# Root build script.
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build --parallel
#   ctest --test-dir build -C RelWithDebInfo --output-on-failure
#
# Products:
#   nnos         (static lib)   — platform abstraction layer
#   sp2harness   (executable)   — cross-platform guest-function execution bed
#   *_tests      (executables)  — port/ unit/integration tests

add_subdirectory(port)
add_subdirectory(harness)
