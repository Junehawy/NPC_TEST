// 运行期开关解析 + 超集约束校验（RA-§5.2）。
// 本二进制与 npc_agent 库同编译期开关（默认 {llm=1, memory_vector=1}），
// 覆盖开关矩阵的 {1,1} 行；其余三行见 tests/matrix/ 三个二进制。
#include "npc_agent/config/feature_flags.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using npc_agent::config::RuntimeFlags;
using npc_agent::config::SwitchError;
using npc_agent::config::parse_and_validate;
using npc_agent::config::to_string;

// 编译期开关行守卫：本二进制必须是 {1,1} 行（RA-§5.2 矩阵测试）。
static_assert(npc_agent::config::CompileFlags::kLlmCompiled);
static_assert(npc_agent::config::CompileFlags::kMemoryVectorCompiled);

namespace {

constexpr std::string_view kSource = "test.json";

// 便捷：断言成功并返回结果（失败时打印完整错误，便于定位）。
RuntimeFlags parse_ok(std::string_view text) {
    RuntimeFlags flags;
    const auto err = parse_and_validate(text, kSource, flags);
    INFO("unexpected error: " << (err ? to_string(*err) : std::string{}));
    REQUIRE(!err.has_value());
    return flags;
}

// 便捷：断言失败并返回错误对象。
SwitchError parse_err(std::string_view text) {
    RuntimeFlags flags;
    const auto err = parse_and_validate(text, kSource, flags);
    REQUIRE(err.has_value());
    return *err;
}

}  // namespace

TEST_CASE("[config] 空配置 → 全部能力默认关闭") {
    const auto flags = parse_ok(R"({})");
    REQUIRE_FALSE(flags.llm_enabled);
    REQUIRE_FALSE(flags.memory_vector_enabled);
}

TEST_CASE("[config] llm 显式启用_编译期已包含 → 通过并生效") {
    const auto flags = parse_ok(R"({"capabilities": {"llm": {"enabled": true}}})");
    REQUIRE(flags.llm_enabled);
    REQUIRE_FALSE(flags.memory_vector_enabled);
}

TEST_CASE("[config] llm 显式禁用 → 通过") {
    const auto flags = parse_ok(R"({"capabilities": {"llm": {"enabled": false}}})");
    REQUIRE_FALSE(flags.llm_enabled);
}

TEST_CASE("[config] memory_vector 显式启用_编译期已包含 → 通过并生效") {
    const auto flags = parse_ok(R"({"capabilities": {"memory_vector": true}})");
    REQUIRE(flags.memory_vector_enabled);
    REQUIRE_FALSE(flags.llm_enabled);
}

TEST_CASE("[config] llm.enabled 类型错误 → 报错且定位到键路径") {
    const auto err = parse_err(R"({"capabilities": {"llm": {"enabled": "yes"}}})");
    REQUIRE(err.file == kSource);
    REQUIRE(err.key_path == "capabilities.llm.enabled");
    REQUIRE(err.expected == "布尔值");
}

TEST_CASE("[config] llm 值非对象 → 报错") {
    const auto err = parse_err(R"({"capabilities": {"llm": 42}})");
    REQUIRE(err.key_path == "capabilities.llm");
}

TEST_CASE("[config] llm 缺 enabled 字段 → 报错") {
    const auto err = parse_err(R"({"capabilities": {"llm": {}}})");
    REQUIRE(err.key_path == "capabilities.llm.enabled");
    REQUIRE(err.actual == "缺失");
}

TEST_CASE("[config] memory_vector 类型错误 → 报错") {
    const auto err = parse_err(R"({"capabilities": {"memory_vector": "on"}})");
    REQUIRE(err.key_path == "capabilities.memory_vector");
    REQUIRE(err.expected == "布尔值");
}

TEST_CASE("[config] 未知能力键 → 报错并列出已知键") {
    const auto err = parse_err(R"({"capabilities": {"combat": true}})");
    REQUIRE(err.key_path == "capabilities.combat");
    REQUIRE(err.expected.find("llm") != std::string::npos);
    REQUIRE(err.actual.find("combat") != std::string::npos);
}

TEST_CASE("[config] 未知顶层键 → 报错") {
    const auto err = parse_err(R"({"foo": 1})");
    REQUIRE(err.key_path == "foo");
    REQUIRE(err.actual.find("foo") != std::string::npos);
}

TEST_CASE("[config] JSON 语法错误 → 报错且包含字节偏移") {
    const auto err = parse_err(R"({"capabilities": })");
    REQUIRE(err.key_path == "<json>");
    REQUIRE(err.actual.find("字节偏移") != std::string::npos);
}

TEST_CASE("[config] 根节点非对象 → 报错") {
    const auto err = parse_err(R"([1, 2, 3])");
    REQUIRE(err.key_path == "<root>");
}

TEST_CASE("[config] capabilities 非对象 → 报错") {
    const auto err = parse_err(R"({"capabilities": []})");
    REQUIRE(err.key_path == "capabilities");
}

TEST_CASE("[config] 错误信息 to_string → 含文件/键路径/期望/实际四要素") {
    const auto err = parse_err(R"({"capabilities": {"llm": {"enabled": "yes"}}})");
    const std::string msg = to_string(err);
    REQUIRE(msg.find("test.json") != std::string::npos);
    REQUIRE(msg.find("capabilities.llm.enabled") != std::string::npos);
    REQUIRE(msg.find("布尔值") != std::string::npos);
    REQUIRE(msg.find("string") != std::string::npos);  // nlohmann type_name("yes") == "string"
}
