// Utility 决策器测试（TS-§3 scenarios 行：计分胜出/条件过滤/确定性噪声/fail-fast）。
#include <catch2/catch_test_macros.hpp>

#include <set>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/decision/utility_decision_maker.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::decision;

namespace {

nlohmann::json two_option_json() {
    return nlohmann::json::parse(R"({
        "options": [
            {"name": "patrol", "base_score": 1.0, "noise_amplitude": 0.0,
             "intent": {"kind": "move_to", "target": [0, 0, 0], "speed": 1.0}},
            {"name": "fight", "base_score": 10.0, "noise_amplitude": 0.0,
             "condition": {"bb": {"enemy_visible": true}},
             "intent": {"kind": "emote", "name": "angry"}}
        ]
    })");
}

TickContext tick(uint64_t seed) {
    TickContext tc;
    tc.dt = 0.1f;
    tc.game_time = 1.0;
    tc.tick_index = 0;
    tc.rng_seed = seed;
    return tc;
}

} // namespace

TEST_CASE("Utility 最高基础分胜出", "[utility]") {
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(two_option_json(), def).has_value());
    UtilityDecisionMaker utility(std::move(def));
    Blackboard bb;
    bb.set("enemy_visible", true);
    const auto intent = utility.propose(bb, tick(42));
    REQUIRE(intent.has_value());
    REQUIRE(std::holds_alternative<EmoteIntent>(intent->payload)); // fight 10 > patrol 1
    REQUIRE(utility.last_picked() == "fight");
    REQUIRE(utility.last_score() == 10.0f);
}

TEST_CASE("Utility 条件过滤：不满足条件的选项不参与", "[utility]") {
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(two_option_json(), def).has_value());
    UtilityDecisionMaker utility(std::move(def));
    Blackboard bb; // 无 enemy_visible
    const auto intent = utility.propose(bb, tick(42));
    REQUIRE(intent.has_value());
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload)); // 仅 patrol 命中
    REQUIRE(utility.last_picked() == "patrol");
}

TEST_CASE("Utility 同种子确定性：两次 propose 结果一致", "[utility]") {
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(two_option_json(), def).has_value());
    UtilityDecisionMaker a(std::move(def));
    UtilityDefinition def2;
    REQUIRE(!parse_utility_definition(two_option_json(), def2).has_value());
    UtilityDecisionMaker b(std::move(def2));
    Blackboard bb;
    bb.set("enemy_visible", true);
    const auto ia = a.propose(bb, tick(7));
    const auto ib = b.propose(bb, tick(7));
    REQUIRE(a.last_picked() == b.last_picked());
    REQUIRE(a.last_score() == b.last_score());
    REQUIRE(ia.has_value() == ib.has_value());
}

TEST_CASE("Utility 噪声确定性：仅由 rng_seed 与选项名决定", "[utility]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "options": [
            {"name": "only", "base_score": 0.0, "noise_amplitude": 1.0,
             "intent": {"kind": "emote", "name": "x"}}
        ]
    })");
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(def_json, def).has_value());
    UtilityDecisionMaker utility(std::move(def));
    Blackboard bb;
    std::set<float> scores;
    for (uint64_t seed = 0; seed < 10; ++seed) {
        REQUIRE(utility.propose(bb, tick(seed)).has_value());
        const float s = utility.last_score();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
        scores.insert(s);
    }
    // 噪声随种子变化：10 个种子至少两个不同值（0.0 恰好的概率可忽略）
    REQUIRE(scores.size() > 1);
    // 同种子复现
    REQUIRE(utility.propose(bb, tick(3)).has_value());
    const float again = utility.last_score();
    REQUIRE(scores.count(again) == 1);
}

TEST_CASE("Utility 待机选项胜出：无权威意图", "[utility]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "options": [
            {"name": "idle_option", "base_score": 5.0,
             "intent": {"kind": "idle"}}
        ]
    })");
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(def_json, def).has_value());
    UtilityDecisionMaker utility(std::move(def));
    Blackboard bb;
    REQUIRE(!utility.propose(bb, tick(0)).has_value());
    REQUIRE(utility.last_picked() == "idle_option");
}

TEST_CASE("Utility 空选项表：无意图", "[utility]") {
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(nlohmann::json{{"options", nlohmann::json::array()}}, def)
                 .has_value());
    UtilityDecisionMaker utility(std::move(def));
    Blackboard bb;
    REQUIRE(!utility.propose(bb, tick(0)).has_value());
    REQUIRE(utility.last_picked().empty());
}

TEST_CASE("Utility 非法定义 fail-fast", "[utility]") {
    UtilityDefinition def;
    // 缺 options
    REQUIRE(parse_utility_definition(nlohmann::json::object(), def).has_value());
    // 选项名重复
    REQUIRE(parse_utility_definition(nlohmann::json::parse(R"({
        "options": [{"name": "a"}, {"name": "a"}]
    })"),
                                     def)
                .has_value());
    // 条件含未知键
    REQUIRE(parse_utility_definition(nlohmann::json::parse(R"({
        "options": [{"name": "a", "condition": {"elapsed_ge": 1.0}}]
    })"),
                                     def)
                .has_value());
    // 负噪声
    REQUIRE(parse_utility_definition(nlohmann::json::parse(R"({
        "options": [{"name": "a", "noise_amplitude": -0.5}]
    })"),
                                     def)
                .has_value());
    // 非法 intent
    REQUIRE(parse_utility_definition(nlohmann::json::parse(R"({
        "options": [{"name": "a", "intent": {"kind": "teleport"}}]
    })"),
                                     def)
                .has_value());
}

TEST_CASE("Utility 序列化：观察字段随存档保留", "[utility]") {
    UtilityDefinition def;
    REQUIRE(!parse_utility_definition(two_option_json(), def).has_value());
    UtilityDecisionMaker utility(std::move(def));
    Blackboard bb;
    bb.set("enemy_visible", true);
    utility.propose(bb, tick(9));

    nlohmann::json saved;
    utility.to_json(saved);
    REQUIRE(saved["last_picked"] == "fight");

    UtilityDefinition def2;
    REQUIRE(!parse_utility_definition(two_option_json(), def2).has_value());
    UtilityDecisionMaker restored(std::move(def2));
    restored.from_json(saved);
    REQUIRE(restored.last_picked() == "fight");
    REQUIRE(restored.last_score() == saved["last_score"].get<float>());
}
