cmake_minimum_required(VERSION 3.28)
project(NPC_TEST LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 三级开关（编译期，RA-§5.2）：编译期开关只对"重依赖/可选第三方库"能力开放。
# 编译期开关是运行期开关的超集约束：编译期关闭的能力，运行期配置
# enabled=true 必须 fail-fast 报错（见 config/feature_flags 与矩阵测试）。
# ---------------------------------------------------------------------------
option(NPC_AGENT_ENABLE_LLM "编译进 LLM 能力（重依赖：HTTP 客户端/本地推理）" ON)
option(NPC_AGENT_ENABLE_MEMORY_VECTOR "编译进记忆向量检索能力（外部向量库）" ON)
option(NPC_AGENT_SANITIZE "以 ASan+UBSan 构建（Debug 下运行测试用）" OFF)

# ---------------------------------------------------------------------------
# 依赖：Catch2 v3 + nlohmann/json（FetchContent 拉取，版本锁定）
# 网络受限时可用镜像前缀覆盖，例如：
#   cmake -DNPC_AGENT_FETCH_BASE=https://ghproxy.net/https://github.com ...
# ---------------------------------------------------------------------------
set(NPC_AGENT_FETCH_BASE "https://github.com" CACHE STRING "FetchContent 依赖下载基址（可指向镜像）")

include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY ${NPC_AGENT_FETCH_BASE}/catchorg/Catch2.git
  GIT_TAG        v3.8.1
  GIT_SHALLOW    TRUE)
FetchContent_Declare(
  json
  GIT_REPOSITORY ${NPC_AGENT_FETCH_BASE}/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2 json)

if(NPC_AGENT_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# npc_agent：智能体核心库（游戏无关；禁止 include game_adapter —— 门禁检查）
# ---------------------------------------------------------------------------
add_library(npc_agent STATIC
  npc_agent/src/config/feature_flags.cpp
  npc_agent/src/interfaces/memory_event.cpp
  npc_agent/src/core/blackboard.cpp
  npc_agent/src/core/agent_config.cpp
  npc_agent/src/core/capability_factory.cpp
  npc_agent/src/core/event.cpp
  npc_agent/src/core/agent.cpp
  npc_agent/src/core/agent_system.cpp)
target_include_directories(npc_agent PUBLIC npc_agent/include)
target_compile_features(npc_agent PUBLIC cxx_std_20)
target_compile_definitions(npc_agent PUBLIC
  NPC_AGENT_ENABLE_LLM=$<BOOL:${NPC_AGENT_ENABLE_LLM}>
  NPC_AGENT_ENABLE_MEMORY_VECTOR=$<BOOL:${NPC_AGENT_ENABLE_MEMORY_VECTOR}>)
target_link_libraries(npc_agent PUBLIC nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# npc_agent_testing：无头测试设施（MockWorld/MockBody/玩具能力，R6-1）
# 测试与示例共用；不参与对外发布。
# ---------------------------------------------------------------------------
add_library(npc_agent_testing STATIC
  npc_agent/src/testing/mock_world.cpp
  npc_agent/src/testing/intent_desc.cpp)
target_include_directories(npc_agent_testing PUBLIC npc_agent/include)
target_link_libraries(npc_agent_testing PUBLIC npc_agent)

# ---------------------------------------------------------------------------
# 示例可执行文件（冒烟用）
# ---------------------------------------------------------------------------
add_executable(npc_test game_adapter/sample_adapter/main.cpp)
target_link_libraries(npc_test PRIVATE npc_agent npc_agent_testing)
# 注入源码根目录：示例从任意工作目录运行时用它兜底定位 assets/
# （见 main.cpp locate_asset_path 的第 3 级候选）。
target_compile_definitions(npc_test PRIVATE
  NPC_TEST_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME npc_test_smoke COMMAND npc_test)
set_tests_properties(npc_test_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# 回归测试：在构建目录运行（复现"从 build/ 直接运行找不到配置"的问题），
# 验证源码根目录兜底路径生效。
add_test(NAME npc_test_anydir_smoke COMMAND npc_test)
set_tests_properties(npc_test_anydir_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------------------------
# 测试（Catch2 + CTest；TS-§1/§2）
# ---------------------------------------------------------------------------
enable_testing()

# 主测试二进制：与 npc_agent 同编译期开关（默认 {llm=1, vec=1} 行）
add_executable(npc_agent_tests
  npc_agent/tests/config/test_feature_flags.cpp
  npc_agent/tests/core/test_blackboard.cpp
  npc_agent/tests/core/test_agent_config.cpp
  npc_agent/tests/core/test_arbitration.cpp
  npc_agent/tests/core/test_agent_system.cpp)
target_link_libraries(npc_agent_tests PRIVATE npc_agent npc_agent_testing Catch2::Catch2WithMain)
add_test(NAME npc_agent_tests COMMAND npc_agent_tests)

# 开关矩阵（RA-§5.2 / TS-§3 matrix 行）：编译期 × 运行期组合全覆盖——
#   npc_agent_tests           覆盖 {llm=1, vec=1}
#   matrix_llm_off_tests      覆盖 {llm=0, vec=1}
#   matrix_vector_off_tests   覆盖 {llm=1, vec=0}
#   matrix_both_off_tests     覆盖 {llm=0, vec=0}
# 各矩阵二进制以目标级编译定义直接重编 feature_flags.cpp（不链接 npc_agent，
# 避免与库 PUBLIC 宏冲突）。

add_executable(matrix_llm_off_tests
  npc_agent/tests/matrix/matrix_llm_off.cpp
  npc_agent/src/config/feature_flags.cpp)
target_include_directories(matrix_llm_off_tests PRIVATE npc_agent/include)
target_compile_definitions(matrix_llm_off_tests PRIVATE
  NPC_AGENT_ENABLE_LLM=0
  NPC_AGENT_ENABLE_MEMORY_VECTOR=1)
target_link_libraries(matrix_llm_off_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
add_test(NAME matrix_llm_off_tests COMMAND matrix_llm_off_tests)

add_executable(matrix_vector_off_tests
  npc_agent/tests/matrix/matrix_vector_off.cpp
  npc_agent/src/config/feature_flags.cpp)
target_include_directories(matrix_vector_off_tests PRIVATE npc_agent/include)
target_compile_definitions(matrix_vector_off_tests PRIVATE
  NPC_AGENT_ENABLE_LLM=1
  NPC_AGENT_ENABLE_MEMORY_VECTOR=0)
target_link_libraries(matrix_vector_off_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
add_test(NAME matrix_vector_off_tests COMMAND matrix_vector_off_tests)

add_executable(matrix_both_off_tests
  npc_agent/tests/matrix/matrix_both_off.cpp
  npc_agent/src/config/feature_flags.cpp)
target_include_directories(matrix_both_off_tests PRIVATE npc_agent/include)
target_compile_definitions(matrix_both_off_tests PRIVATE
  NPC_AGENT_ENABLE_LLM=0
  NPC_AGENT_ENABLE_MEMORY_VECTOR=0)
target_link_libraries(matrix_both_off_tests PRIVATE Catch2::Catch2WithMain nlohmann_json::nlohmann_json)
add_test(NAME matrix_both_off_tests COMMAND matrix_both_off_tests)

# ---------------------------------------------------------------------------
# Godot 2D 集成示例（可选，RA-§8.2 game_adapter/godot_demo；阶段 6 种子）
# 构建：cmake --preset godot && cmake --build --preset godot
# 运行：godot --path game_adapter/godot_demo
# 说明：godot-cpp 为第三方依赖，本预设不施加 -Werror（核心库零警告由
# release/asan 预设门禁保证）；示例自身目标仍带 -Wall -Wextra -Wpedantic -Werror。
# ---------------------------------------------------------------------------
option(NPC_AGENT_BUILD_GODOT_DEMO "构建 Godot 2D 集成示例（GDExtension）" OFF)

if(NPC_AGENT_BUILD_GODOT_DEMO)
  # godot-cpp 4.5-stable：按 GDExtension 前向兼容契约可被 Godot ≥4.5 运行时加载
  # （本地 4.7.1 验证通过）；SYSTEM 头抑制三方告警，关闭热重载与模板调试特性。
  set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "" FORCE)
  set(GODOTCPP_USE_HOT_RELOAD OFF CACHE BOOL "" FORCE)
  # 动态链接 libstdc++：本机未安装静态库；发布扩展时可按需改为静态（-static-libstdc++）。
  set(GODOTCPP_USE_STATIC_CPP OFF CACHE BOOL "" FORCE)
  set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)
  FetchContent_Declare(
    godotcpp
    GIT_REPOSITORY ${NPC_AGENT_FETCH_BASE}/godotengine/godot-cpp.git
    GIT_TAG        godot-4.5-stable
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(godotcpp)

  # 静态库需链入共享扩展，开启 PIC（仅本预设生效，不影响门禁构建产物）。
  set_target_properties(npc_agent npc_agent_testing PROPERTIES POSITION_INDEPENDENT_CODE ON)

  add_library(npc_agent_godot_demo SHARED
    game_adapter/godot_demo/src/godot_world.cpp
    game_adapter/godot_demo/src/godot_body.cpp
    game_adapter/godot_demo/src/guard_patrol_decision.cpp
    game_adapter/godot_demo/src/npc_agent_demo_node.cpp
    game_adapter/godot_demo/src/register_types.cpp
    game_adapter/godot_demo/src/demo_extension.cpp)
  target_include_directories(npc_agent_godot_demo PRIVATE game_adapter/godot_demo/src)
  target_compile_features(npc_agent_godot_demo PRIVATE cxx_std_20)
  target_link_libraries(npc_agent_godot_demo PRIVATE npc_agent npc_agent_testing godot::cpp)
  target_compile_options(npc_agent_godot_demo PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(npc_agent_godot_demo PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "npc_agent_godot_demo.linux.release.x86_64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/game_adapter/godot_demo/bin")
endif()
