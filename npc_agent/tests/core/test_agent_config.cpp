// AgentConfig 解析与 fail-fast 校验测试（RA-§5.2 风格）。
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "npc_agent/core/agent_config.h"

using namespace npc_agent::core;

TEST_CASE("[agent_config] 完整字段 → 解析成功") {
    const nlohmann::json in{{"id", "guard_1"},
                            {"decision", "toy_patrol"},
                            {"rng_seed", 42},
                            {"perception", {{"radius", 10.0}, {"sense_type", "hearing"}}}};
    AgentConfig cfg;
    const auto err = parse_agent_config(in, "t.json", cfg);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(cfg.id == "guard_1");
    REQUIRE(cfg.decision_kind == "toy_patrol");
    REQUIRE(cfg.rng_seed == 42);
    REQUIRE(cfg.perception.radius == 10.0f);
    REQUIRE(cfg.perception.sense_type == "hearing");
}

TEST_CASE("[agent_config] 最小配置_仅 id → 默认值") {
    const nlohmann::json in{{"id", "npc"}};
    AgentConfig cfg;
    const auto err = parse_agent_config(in, "t.json", cfg);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(cfg.decision_kind == "none");
    REQUIRE(cfg.rng_seed == 0);
    REQUIRE(cfg.perception.radius == 0.0f);
}

TEST_CASE("[agent_config] 缺 id → 报错定位") {
    AgentConfig cfg;
    const auto err = parse_agent_config(nlohmann::json::object(), "t.json", cfg);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "id");
}

TEST_CASE("[agent_config] rng_seed 类型错误 → 报错定位") {
    AgentConfig cfg;
    const auto err = parse_agent_config({{"id", "n"}, {"rng_seed", -1}}, "t.json", cfg);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "rng_seed");
}

TEST_CASE("[agent_config] 未知键 → fail-fast 报错") {
    AgentConfig cfg;
    const auto err = parse_agent_config({{"id", "n"}, {"foo", 1}}, "t.json", cfg);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "foo");
}

TEST_CASE("[agent_config] perception.radius 负数 → 报错定位") {
    AgentConfig cfg;
    const auto err =
        parse_agent_config({{"id", "n"}, {"perception", {{"radius", -1.0}}}}, "t.json", cfg);
    REQUIRE(err.has_value());
    REQUIRE(err->key_path == "perception.radius");
}
