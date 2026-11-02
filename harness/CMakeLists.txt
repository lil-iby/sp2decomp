cmake_minimum_required(VERSION 3.20)

# harness — cross-platform guest-function execution test bed.
#
# Builds a single executable (sp2harness) that:
#   1. Maps guest memory at 0x7100000000.
#   2. Installs a fault handler (SEH on Windows, sigaction on Linux/macOS).
#   3. Calls the first recompiled guest function and reports register state.
#
# The harness links against nnos (the platform abstraction library) from the
# sibling port/ subdirectory so that nn:: / sead:: calls resolve to stubs
# rather than crashing immediately.
#
# To test a real recompiled function: add its .cpp to the GUEST_SOURCES list
# below, or pass -DSP2_GUEST_SOURCES="path/to/fn.cpp" on the cmake command line.

set(SP2_GUEST_SOURCES "" CACHE STRING "Semicolon-separated list of extra guest TUs to link into the harness")

add_executable(sp2harness
  harness.cpp
  ${SP2_GUEST_SOURCES}
)

target_include_directories(sp2harness PRIVATE
  ../port/include
)

target_link_libraries(sp2harness PRIVATE nnos)

set_target_properties(sp2harness PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(sp2harness PRIVATE /W4)
  # SEH requires that exceptions are either enabled or specifically suppressed;
  # /EHa enables both C++ and SEH exceptions.
  target_compile_options(sp2harness PRIVATE /EHa)
else()
  target_compile_options(sp2harness PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Optional: register a ctest that just confirms the harness starts and
# terminates without an access violation (exit code 0 or 1 are both ok;
# code 2+ indicates a crash).
add_test(NAME harness_smoke COMMAND sp2harness)
set_tests_properties(harness_smoke PROPERTIES WILL_FAIL false TIMEOUT 10)
