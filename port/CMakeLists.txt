cmake_minimum_required(VERSION 3.20)
project(sp2port LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_library(nnos STATIC
  src/os.cpp
  src/util.cpp
  src/services.cpp
  src/vi.cpp
  src/nvn.cpp
  src/nvn_names.cpp
  src/nvn_bridge.cpp
  src/task_base.cpp
  src/graphics_allocator.cpp
  src/init.cpp
  src/date_time.cpp
  src/utl.cpp
  src/boot_sequence.cpp
  src/delegate_event.cpp
  src/framework_args.cpp
  src/heap.cpp
  src/heap_mgr.cpp
  src/thread_mgr.cpp
  src/framework.cpp
  src/game_framework.cpp
  src/game_framework_nx.cpp
  src/lock.cpp
  src/heap_group.cpp
  # Platform abstraction stubs added for clean-architecture port
  src/hid.cpp
  src/audio.cpp
  src/nifm.cpp
  src/account.cpp
  src/oe.cpp
  src/worker_thread_mgr.cpp
  src/root_task.cpp
  src/sys_init.cpp
  src/safe_string.cpp
  src/lp_framework.cpp
)
target_include_directories(nnos PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(nnos PUBLIC Threads::Threads)

# Vulkan rendering backend (disabled by default; the stub backend is always sufficient for CI).
# Build locally: cmake -DNVN_BACKEND_VULKAN=ON <src>   (requires the Vulkan SDK).
option(NVN_BACKEND_VULKAN "Build the NVN→Vulkan rendering backend (requires Vulkan SDK)" OFF)
if(NVN_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(nnos PRIVATE Vulkan::Vulkan)
    target_compile_definitions(nnos PRIVATE NVN_BACKEND_VULKAN)
    message(STATUS "NVN backend: Vulkan (${Vulkan_VERSION})")
else()
    message(STATUS "NVN backend: Stub (default)")
endif()

# Warnings that matter for ABI-faithful reconstruction work.
if(MSVC)
  target_compile_options(nnos PRIVATE /W4)
else()
  target_compile_options(nnos PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(SP2PORT_BUILD_TESTS "Build the platform contract tests" ON)
if(SP2PORT_BUILD_TESTS)
  enable_testing()
  add_executable(platform_tests tests/platform_contract_tests.cpp)
  target_link_libraries(platform_tests PRIVATE nnos)
  add_test(NAME platform_contract COMMAND platform_tests)

  add_executable(services_tests tests/services_tests.cpp)
  target_link_libraries(services_tests PRIVATE nnos)
  add_test(NAME services_contract COMMAND services_tests)

  # Replays the real boot sequence rather than testing functions in isolation.
  add_executable(boot_integration tests/boot_integration.cpp)
  target_link_libraries(boot_integration PRIVATE nnos)
  add_test(NAME boot_sequence COMMAND boot_integration)

  # Diagnostic: prints candidate struct offsets so decompile-derived layouts can be
  # matched empirically instead of guessed. Not a test.
  add_executable(layout_probe tests/layout_probe.cpp)
  target_link_libraries(layout_probe PRIVATE nnos)
  add_executable(runarg_probe tests/runarg_probe.cpp)
  target_link_libraries(runarg_probe PRIVATE nnos)

  # The first RECONSTRUCTED ENGINE functions (ports of decompiled bodies).
  add_executable(engine_tests tests/engine_tests.cpp)
  target_link_libraries(engine_tests PRIVATE nnos)
  add_test(NAME engine_contract COMMAND engine_tests)

  # sead::ListNode -- the intrusive list primitive registerCallback needs.
  add_executable(list_node_tests tests/list_node_tests.cpp)
  target_link_libraries(list_node_tests PRIVATE nnos)
  add_test(NAME list_node_contract COMMAND list_node_tests)

  # Runs reconstructed engine code and reports the first missing dependency.
  add_executable(boot_engine_tests tests/boot_engine_tests.cpp)
  target_link_libraries(boot_engine_tests PRIVATE nnos)
  add_test(NAME boot_engine COMMAND boot_engine_tests)

  # sead::AssertConfig::registerCallback -- port of 0x7101397fac.
  add_executable(delegate_event_tests tests/delegate_event_tests.cpp)
  target_link_libraries(delegate_event_tests PRIVATE nnos)
  add_test(NAME delegate_event_contract COMMAND delegate_event_tests)

  # sead::Framework::InitializeArg / RunArg constructors.
  add_executable(framework_args_tests tests/framework_args_tests.cpp)
  target_link_libraries(framework_args_tests PRIVATE nnos)
  add_test(NAME framework_args_contract COMMAND framework_args_tests)

  # sead::Heap / ExpHeap -- the interface over the host allocator.
  add_executable(heap_tests tests/heap_tests.cpp)
  target_link_libraries(heap_tests PRIVATE nnos)
  add_test(NAME heap_contract COMMAND heap_tests)

  # sead::HeapMgr -- the boot-entry heap manager.
  add_executable(heap_mgr_tests tests/heap_mgr_tests.cpp)
  target_link_libraries(heap_mgr_tests PRIVATE nnos)
  add_test(NAME heap_mgr_contract COMMAND heap_mgr_tests)

  # sead::Framework::initialize -- first consumer of InitializeArg + HeapMgr.
  add_executable(framework_tests tests/framework_tests.cpp)
  target_link_libraries(framework_tests PRIVATE nnos)
  add_test(NAME framework_contract COMMAND framework_tests)

  # sead::GameFramework / GameFrameworkNx::initialize -- boot step 5.
  add_executable(game_framework_tests tests/game_framework_tests.cpp)
  target_link_libraries(game_framework_tests PRIVATE nnos)
  add_test(NAME game_framework_contract COMMAND game_framework_tests)

  # Lp::Sys::HeapGroup + sead::ForegroundBucketMgr.
  add_executable(heap_group_tests tests/heap_group_tests.cpp)
  target_link_libraries(heap_group_tests PRIVATE nnos)
  add_test(NAME heap_group_contract COMMAND heap_group_tests)

  # nn::vi display boundary + nvn::LoadCProcs dispatch table.
  add_executable(vi_tests tests/vi_tests.cpp)
  target_link_libraries(vi_tests PRIVATE nnos)
  add_test(NAME vi_contract COMMAND vi_tests)

  # sead::TaskBase::MgrTaskArg -- the last non-graphics boot-root dependency.
  add_executable(task_base_tests tests/task_base_tests.cpp)
  target_link_libraries(task_base_tests PRIVATE nnos)
  add_test(NAME task_base_contract COMMAND task_base_tests)

  # nv::SetGraphicsAllocator + nn::mem::StandardAllocator -- the graphics entry.
  add_executable(graphics_allocator_tests tests/graphics_allocator_tests.cpp)
  target_link_libraries(graphics_allocator_tests PRIVATE nnos)
  add_test(NAME graphics_allocator_contract COMMAND graphics_allocator_tests)

  # sead::Framework::run -- the main game loop (step 15: bounded loop + requestStop).
  add_executable(run_loop_tests tests/run_loop_tests.cpp)
  target_link_libraries(run_loop_tests PRIVATE nnos)
  add_test(NAME run_loop COMMAND run_loop_tests)

  # Lp::Sys::WorkerThreadMgr -- createWorker / enqueue / run dispatch.
  add_executable(worker_thread_mgr_tests tests/worker_thread_mgr_tests.cpp)
  target_link_libraries(worker_thread_mgr_tests PRIVATE nnos)
  add_test(NAME worker_thread_mgr COMMAND worker_thread_mgr_tests)

  # Lp::Sys::RootTask -- parallel-init orchestration (create / run / waitDone).
  add_executable(root_task_tests tests/root_task_tests.cpp)
  target_link_libraries(root_task_tests PRIVATE nnos)
  add_test(NAME root_task COMMAND root_task_tests)

  # Lp::Sys::SysInit -- nninitStartup (step 0) + getCpCallbackSeadAssert + printFromStartTime.
  add_executable(sys_init_tests tests/sys_init_tests.cpp)
  target_link_libraries(sys_init_tests PRIVATE nnos)
  add_test(NAME sys_init COMMAND sys_init_tests)

  # sead::SafeStringBase / BufferedSafeStringBase / FormatFixedSafeString.
  add_executable(safe_string_tests tests/safe_string_tests.cpp)
  target_link_libraries(safe_string_tests PRIVATE nnos)
  add_test(NAME safe_string COMMAND safe_string_tests)

  # Lp::Sys::Framework ctor (0x71011f44e4, depth 1, 108 bytes).
  add_executable(lp_framework_tests tests/lp_framework_tests.cpp)
  target_link_libraries(lp_framework_tests PRIVATE nnos)
  add_test(NAME lp_framework COMMAND lp_framework_tests)

endif()
