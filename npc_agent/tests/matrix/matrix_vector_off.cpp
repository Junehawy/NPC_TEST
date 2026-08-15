// 开关矩阵 {llm=1, memory_vector=0} 行（RA-§5.2 / TS-§3 matrix 行）。
// 本二进制以 NPC_AGENT_ENABLE_MEMORY_VECTOR=0 直接重编 feature_flags.cpp（见 CMake）。
#include "npc_agent/config/feature_flags.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using npc_agent::config::parse_and_validate;
using npc_agent::config::RuntimeFlags;

// 编译期开关行守卫：本二进制必须是 {1,0} 行。
static_assert(npc_agent::config::CompileFlags::kLlmCompiled);
static_assert(!npc_agent::config::CompileFlags::kMemoryVectorCompiled);

namespace {

constexpr std::string_view kSource = "matrix_vector_off.json";

RuntimeFlags parse_ok(std::string_view text) {
    RuntimeFlags flags;
    const auto err = parse_and_validate(text, kSource, flags);
    REQUIRE_FALSE(err.has_value());
    return flags;
}

} // namespace

TEST_CASE("[matrix] memory_vector 编译期关闭_运行期启用 → fail-fast 报错") {
    RuntimeFlags flags;
    const auto err =
        parse_and_validate(R"({"capabilities": {"memory_vector": true}})", kSource, flags);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "capabilities.memory_vector");
    REQUIRE(err->expected.find("NPC_AGENT_ENABLE_MEMORY_VECTOR") != std::string::npos);
    REQUIRE(err->actual == "true");
}

TEST_CASE("[matrix] memory_vector 编译期关闭_运行期禁用 → 通过") {
    const auto flags = parse_ok(R"({"capabilities": {"memory_vector": false}})");
    REQUIRE_FALSE(flags.memory_vector_enabled);
}

TEST_CASE("[matrix] memory_vector 编译期关闭_键缺席 → 通过且默认关") {
    const auto flags = parse_ok(R"({})");
    REQUIRE_FALSE(flags.memory_vector_enabled);
}

TEST_CASE("[matrix] 对照能力 llm 编译期包含_运行期启用 → 通过") {
    const auto flags = parse_ok(R"({"capabilities": {"llm": {"enabled": true}}})");
    REQUIRE(flags.llm_enabled);
}
