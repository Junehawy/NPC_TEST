// FSM 决策器测试（TS-§3 scenarios 行：状态迁移/意图模板/fail-fast/序列化）。
#include <catch2/catch_test_macros.hpp>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/decision/fsm_decision_maker.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::decision;

namespace {

// 验收场景同构的最小定义：idle →(alarm)→ alert →(elapsed_ge 2s)→ idle。
nlohmann::json minimal_def_json() {
    return nlohmann::json::parse(R"({
        "initial": "idle",
        "states": [
            {
                "name": "idle",
                "intent": {"kind": "idle"},
                "transitions": [
                    {"target": "alert", "condition": {"bb": {"alarm": true}}}
                ]
            },
            {
                "name": "alert",
                "priority": 5.0,
                "intent": {"kind": "emote", "name": "startled"},
                "transitions": [
                    {"target": "idle", "condition": {"elapsed_ge": 2.0}}
                ]
            }
        ]
    })");
}

FsmDecisionMaker make_minimal() {
    FsmDefinition def;
    REQUIRE(!parse_fsm_definition(minimal_def_json(), def).has_value());
    return FsmDecisionMaker(std::move(def));
}

TickContext tick(double time) {
    TickContext tc;
    tc.dt = 0.1f;
    tc.game_time = time;
    tc.tick_index = 0;
    tc.rng_seed = 0;
    return tc;
}

} // namespace

TEST_CASE("FSM 初始状态 idle 不产出权威意图", "[fsm]") {
    auto fsm = make_minimal();
    Blackboard bb;
    const auto intent = fsm.propose(bb, tick(0.0));
    REQUIRE(!intent.has_value());
    REQUIRE(fsm.current_state() == "idle");
}

TEST_CASE("FSM 黑板条件迁移：alarm → alert 产出表情意图", "[fsm]") {
    auto fsm = make_minimal();
    Blackboard bb;
    fsm.propose(bb, tick(0.0)); // 首 tick 仅记录进入时刻
    bb.set("alarm", true);
    const auto intent = fsm.propose(bb, tick(0.1));
    REQUIRE(intent.has_value());
    REQUIRE(std::holds_alternative<EmoteIntent>(intent->payload));
    REQUIRE(std::get<EmoteIntent>(intent->payload).name == "startled");
    REQUIRE(intent->priority == 5.0f);
    REQUIRE(fsm.current_state() == "alert");
}

TEST_CASE("FSM 停留时长迁移：elapsed_ge 到期返回 idle", "[fsm]") {
    auto fsm = make_minimal();
    Blackboard bb;
    bb.set("alarm", true);
    fsm.propose(bb, tick(0.0));
    REQUIRE(fsm.current_state() == "idle");          // 首 tick 不评估迁移
    REQUIRE(fsm.propose(bb, tick(0.1)).has_value()); // 迁移到 alert
    REQUIRE(fsm.current_state() == "alert");
    fsm.propose(bb, tick(0.5)); // 停留 0.4s < 2.0，仍在 alert
    REQUIRE(fsm.current_state() == "alert");
    const auto intent = fsm.propose(bb, tick(3.0)); // 停留 2.9s ≥ 2.0 → idle
    REQUIRE(!intent.has_value());
    REQUIRE(fsm.current_state() == "idle");
}

TEST_CASE("FSM 兜底迁移：default 恒真", "[fsm]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "initial": "a",
        "states": [
            {"name": "a", "intent": {"kind": "idle"},
             "transitions": [{"target": "b", "condition": {"default": true}}]},
            {"name": "b", "intent": {"kind": "say", "text": "转移"}}
        ]
    })");
    FsmDefinition def;
    REQUIRE(!parse_fsm_definition(def_json, def).has_value());
    FsmDecisionMaker fsm(std::move(def));
    Blackboard bb;
    fsm.propose(bb, tick(0.0));
    const auto intent = fsm.propose(bb, tick(0.1));
    REQUIRE(intent.has_value());
    REQUIRE(std::holds_alternative<SayIntent>(intent->payload));
    REQUIRE(fsm.current_state() == "b");
}

TEST_CASE("FSM 意图模板：move_to/say/game_event 解析", "[fsm]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "initial": "move",
        "states": [
            {"name": "move",
             "intent": {"kind": "move_to", "target": [1, 2, 3], "speed": 2.5}},
            {"name": "talk",
             "intent": {"kind": "say", "text": "呼叫支援", "tone": "urgent"}},
            {"name": "call",
             "intent": {"kind": "game_event", "type": "call_support", "payload": {"n": 2}}}
        ]
    })");
    FsmDefinition def;
    REQUIRE(!parse_fsm_definition(def_json, def).has_value());
    REQUIRE(def.states.size() == 3);
    // move_to 模板
    REQUIRE(def.states[0].intent.has_value());
    const auto& move = std::get<MoveIntent>(def.states[0].intent->payload);
    REQUIRE(move.target.x == 1.0f);
    REQUIRE(move.target.y == 2.0f);
    REQUIRE(move.target.z == 3.0f);
    REQUIRE(move.speed == 2.5f);
    // say 模板
    const auto& say = std::get<SayIntent>(def.states[1].intent->payload);
    REQUIRE(say.text == "呼叫支援");
    REQUIRE(say.tone == "urgent");
    // game_event 模板
    const auto& call = std::get<GameEventIntent>(def.states[2].intent->payload);
    REQUIRE(call.event.type == "call_support");
    REQUIRE(call.event.payload["n"] == 2);
}

TEST_CASE("FSM 非法定义 fail-fast", "[fsm]") {
    FsmDefinition def;
    // 缺 initial
    REQUIRE(
        parse_fsm_definition(nlohmann::json{{"states", nlohmann::json::array()}}, def).has_value());
    // 未知迁移目标
    REQUIRE(parse_fsm_definition(nlohmann::json::parse(R"({
        "initial": "a",
        "states": [{"name": "a", "transitions": [{"target": "b"}]}]
    })"),
                                 def)
                .has_value());
    // 状态名重复
    REQUIRE(parse_fsm_definition(nlohmann::json::parse(R"({
        "initial": "a",
        "states": [{"name": "a"}, {"name": "a"}]
    })"),
                                 def)
                .has_value());
    // 非法 intent kind
    REQUIRE(parse_fsm_definition(nlohmann::json::parse(R"({
        "initial": "a",
        "states": [{"name": "a", "intent": {"kind": "teleport"}}]
    })"),
                                 def)
                .has_value());
}

TEST_CASE("FSM 序列化：当前状态与进入时刻随存档保留", "[fsm]") {
    auto fsm = make_minimal();
    Blackboard bb;
    bb.set("alarm", true);
    fsm.propose(bb, tick(0.0));
    fsm.propose(bb, tick(0.1)); // 迁移到 alert（entered_at = 0.1）
    REQUIRE(fsm.current_state() == "alert");

    nlohmann::json saved;
    fsm.to_json(saved);
    REQUIRE(saved["current"] == "alert");
    REQUIRE(saved["entered_at"].get<double>() == 0.1);

    // 恢复后停留在 alert，elapsed 继续累计
    FsmDefinition def;
    REQUIRE(!parse_fsm_definition(minimal_def_json(), def).has_value());
    FsmDecisionMaker restored(std::move(def));
    restored.from_json(saved);
    REQUIRE(restored.current_state() == "alert");
    // 距 entered_at=0.1 已过 1.0s < 2.0 → 仍在 alert
    restored.propose(bb, tick(1.1));
    REQUIRE(restored.current_state() == "alert");
}

TEST_CASE("FSM 条件不满足时停留当前状态", "[fsm]") {
    auto fsm = make_minimal();
    Blackboard bb; // 无 alarm
    fsm.propose(bb, tick(0.0));
    fsm.propose(bb, tick(1.0));
    REQUIRE(fsm.current_state() == "idle");
}
