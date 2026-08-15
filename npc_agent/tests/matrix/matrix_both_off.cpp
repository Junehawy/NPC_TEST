// 开关矩阵 {llm=0, memory_vector=0} 行（RA-§5.2 / TS-§3 matrix 行）。
// 本二进制以两个能力均 OFF 直接重编 feature_flags.cpp（见 CMake）。
// 覆盖语义：全部能力编译期关闭时，运行期任何 enabled=true 都必须报错。
#include "npc_agent/config/feature_flags.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using npc_agent::config::RuntimeFlags;
using npc_agent::config::parse_and_validate;

// 编译期开关行守卫：本二进制必须是 {0,0} 行。
static_assert(!npc_agent::config::CompileFlags::kLlmCompiled);
static_assert(!npc_agent::config::CompileFlags::kMemoryVectorCompiled);

namespace {

constexpr std::string_view kSource = "matrix_both_off.json";

}  // namespace

TEST_CASE("[matrix] 全部编译期关闭_llm 运行期启用 → fail-fast 报错") {
    RuntimeFlags flags;
    const auto err =
        parse_and_validate(R"({"capabilities": {"llm": {"enabled": true}}})", kSource, flags);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "capabilities.llm.enabled");
}

TEST_CASE("[matrix] 全部编译期关闭_memory_vector 运行期启用 → fail-fast 报错") {
    RuntimeFlags flags;
    const auto err = parse_and_validate(R"({"capabilities": {"memory_vector": true}})",
                                        kSource, flags);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "capabilities.memory_vector");
}

TEST_CASE("[matrix] 全部编译期关闭_运行期全禁用 → 通过且默认关") {
    RuntimeFlags flags;
    const auto err = parse_and_validate(
        R"({"capabilities": {"llm": {"enabled": false}, "memory_vector": false}})", kSource,
        flags);
    REQUIRE_FALSE(err.has_value());
    REQUIRE_FALSE(flags.llm_enabled);
    REQUIRE_FALSE(flags.memory_vector_enabled);
}
