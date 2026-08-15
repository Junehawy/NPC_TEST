// 开关矩阵 {llm=0, memory_vector=1} 行（RA-§5.2 / TS-§3 matrix 行）。
// 本二进制以 NPC_AGENT_ENABLE_LLM=0 直接重编 feature_flags.cpp（见 CMake）。
// 覆盖语义：编译期关闭的能力，运行期 enabled=true 必须 fail-fast 报错。
#include "npc_agent/config/feature_flags.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using npc_agent::config::RuntimeFlags;
using npc_agent::config::parse_and_validate;

// 编译期开关行守卫：本二进制必须是 {0,1} 行。
static_assert(!npc_agent::config::CompileFlags::kLlmCompiled);
static_assert(npc_agent::config::CompileFlags::kMemoryVectorCompiled);

namespace {

constexpr std::string_view kSource = "matrix_llm_off.json";

// 便捷：解析一次。
RuntimeFlags parse_ok(std::string_view text) {
    RuntimeFlags flags;
    const auto err = parse_and_validate(text, kSource, flags);
    REQUIRE_FALSE(err.has_value());
    return flags;
}

}  // namespace

TEST_CASE("[matrix] llm 编译期关闭_运行期启用 → fail-fast 报错") {
    RuntimeFlags flags;
    const auto err =
        parse_and_validate(R"({"capabilities": {"llm": {"enabled": true}}})", kSource, flags);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "capabilities.llm.enabled");
    REQUIRE(err->expected.find("NPC_AGENT_ENABLE_LLM") != std::string::npos);
    REQUIRE(err->actual == "true");
}

TEST_CASE("[matrix] llm 编译期关闭_运行期禁用 → 通过") {
    const auto flags = parse_ok(R"({"capabilities": {"llm": {"enabled": false}}})");
    REQUIRE_FALSE(flags.llm_enabled);
}

TEST_CASE("[matrix] llm 编译期关闭_键缺席 → 通过且默认关") {
    const auto flags = parse_ok(R"({})");
    REQUIRE_FALSE(flags.llm_enabled);
}

TEST_CASE("[matrix] 对照能力 memory_vector 编译期包含_运行期启用 → 通过") {
    const auto flags = parse_ok(R"({"capabilities": {"memory_vector": true}})");
    REQUIRE(flags.memory_vector_enabled);
}
