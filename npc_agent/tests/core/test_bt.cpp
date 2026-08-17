// BT 决策器测试（TS-§3 scenarios 行：回退/序列推进/循环/读档重置/fail-fast）。
#include <catch2/catch_test_macros.hpp>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/decision/bt_decision_maker.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::decision;

namespace {

// 验收场景同构：警戒(emote) →(elapsed 1s)→ 呼叫支援(say) →(bb support_called)→ 搜寻(move)。
nlohmann::json acceptance_tree_json() {
    return nlohmann::json::parse(R"({
        "root": {
            "kind": "sequence",
            "children": [
                {"kind": "action",
                 "intent": {"kind": "emote", "name": "startled"},
                 "done_when": {"elapsed_ge": 1.0}},
                {"kind": "action",
                 "intent": {"kind": "say", "text": "呼叫支援", "tone": "urgent"},
                 "done_when": {"bb": {"support_called": true}}},
                {"kind": "action",
                 "intent": {"kind": "move_to", "target": [5, 0, 0], "speed": 1.5}}
            ]
        }
    })");
}

BtDecisionMaker make_tree(const nlohmann::json& def_json) {
    BtNode root;
    REQUIRE(!parse_bt_definition(def_json, root).has_value());
    return BtDecisionMaker(std::move(root));
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

TEST_CASE("BT 序列相位推进：elapsed_ge 与 bb 依次推进", "[bt]") {
    auto bt = make_tree(acceptance_tree_json());
    Blackboard bb;
    // t=0：首相位 警戒
    auto intent = bt.propose(bb, tick(0.0));
    REQUIRE(intent.has_value());
    REQUIRE(std::get<EmoteIntent>(intent->payload).name == "startled");
    REQUIRE(bt.current_path() == "root/0");
    // t=0.5：未满 1s，仍在警戒
    intent = bt.propose(bb, tick(0.5));
    REQUIRE(std::holds_alternative<EmoteIntent>(intent->payload));
    // t=1.0：elapsed 1.0 → 推进到呼叫支援
    intent = bt.propose(bb, tick(1.0));
    REQUIRE(std::holds_alternative<SayIntent>(intent->payload));
    REQUIRE(std::get<SayIntent>(intent->payload).text == "呼叫支援");
    REQUIRE(bt.current_path() == "root/1");
    // 黑板条件满足 → 推进到搜寻
    bb.set("support_called", true);
    intent = bt.propose(bb, tick(1.1));
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload));
    REQUIRE(bt.current_path() == "root/2");
    // 末相位稳定驻留（无 done_when）
    intent = bt.propose(bb, tick(2.0));
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload));
    REQUIRE(bt.current_path() == "root/2");
}

TEST_CASE("BT 序列循环：末子节点完成后回到首子节点", "[bt]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "root": {"kind": "sequence", "children": [
            {"kind": "action", "intent": {"kind": "emote", "name": "a"},
             "done_when": {"elapsed_ge": 0.5}},
            {"kind": "action", "intent": {"kind": "emote", "name": "b"},
             "done_when": {"elapsed_ge": 0.5}}
        ]}
    })");
    auto bt = make_tree(def_json);
    Blackboard bb;
    REQUIRE(std::get<EmoteIntent>(bt.propose(bb, tick(0.0))->payload).name == "a");
    REQUIRE(std::get<EmoteIntent>(bt.propose(bb, tick(0.5))->payload).name == "b");
    REQUIRE(std::get<EmoteIntent>(bt.propose(bb, tick(1.0))->payload).name == "a"); // 循环
    REQUIRE(bt.current_path() == "root/0");
}

TEST_CASE("BT 读档重置：from_json 后回到根节点重评（决策表 #19）", "[bt]") {
    auto bt = make_tree(acceptance_tree_json());
    Blackboard bb;
    bb.set("support_called", true);
    bt.propose(bb, tick(0.0)); // 警戒
    bt.propose(bb, tick(1.0)); // 呼叫支援
    bt.propose(bb, tick(1.1)); // 搜寻（root/2）
    REQUIRE(bt.current_path() == "root/2");

    nlohmann::json saved;
    bt.to_json(saved);
    REQUIRE(saved.is_object());

    bt.from_json(saved); // 重置到根
    const auto intent = bt.propose(bb, tick(2.0));
    REQUIRE(std::holds_alternative<EmoteIntent>(intent->payload)); // 回到首相位
    REQUIRE(bt.current_path() == "root/0");
}

TEST_CASE("BT selector 条件回退：enemy_visible 切换分支", "[bt]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "root": {"kind": "selector", "children": [
            {"kind": "condition", "condition": {"bb": {"enemy_visible": true}},
             "child": {"kind": "action",
                       "intent": {"kind": "emote", "name": "angry"}}},
            {"kind": "action", "intent": {"kind": "move_to", "target": [0, 0, 0]}}
        ]}
    })");
    auto bt = make_tree(def_json);
    Blackboard bb;
    auto intent = bt.propose(bb, tick(0.0)); // 无 enemy_visible → 巡逻分支
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload));
    REQUIRE(bt.current_path() == "root/1");
    bb.set("enemy_visible", true);
    intent = bt.propose(bb, tick(0.1)); // 条件成立 → 战斗分支（结构路径含 condition 层）
    REQUIRE(std::holds_alternative<EmoteIntent>(intent->payload));
    REQUIRE(bt.current_path() == "root/0/0");
    bb.set("enemy_visible", false);
    intent = bt.propose(bb, tick(0.2)); // 条件撤销 → 回退巡逻
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload));
    REQUIRE(bt.current_path() == "root/1");
}

TEST_CASE("BT 序列条件失败：整序不可运行，selector 兜底", "[bt]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "root": {"kind": "selector", "children": [
            {"kind": "sequence", "children": [
                {"kind": "condition", "condition": {"bb": {"alarm": true}},
                 "child": {"kind": "action",
                           "intent": {"kind": "emote", "name": "警戒"}}},
                {"kind": "action", "intent": {"kind": "say", "text": "支援"}}
            ]},
            {"kind": "action", "intent": {"kind": "emote", "name": "fallback"}}
        ]}
    })");
    auto bt = make_tree(def_json);
    Blackboard bb; // 无 alarm → sequence 不可运行 → 兜底分支
    auto intent = bt.propose(bb, tick(0.0));
    REQUIRE(std::get<EmoteIntent>(intent->payload).name == "fallback");
    REQUIRE(bt.current_path() == "root/1");
    bb.set("alarm", true); // sequence 可运行 → 切换到 sequence 首子节点
    intent = bt.propose(bb, tick(0.1));
    REQUIRE(std::get<EmoteIntent>(intent->payload).name == "警戒");
    REQUIRE(bt.current_path() == "root/0/0/0");
}

TEST_CASE("BT idle 叶子：无权威意图", "[bt]") {
    const nlohmann::json def_json = nlohmann::json::parse(R"({
        "root": {"kind": "selector", "children": [
            {"kind": "action", "intent": {"kind": "idle"}},
            {"kind": "action", "intent": {"kind": "emote", "name": "busy"}}
        ]}
    })");
    auto bt = make_tree(def_json);
    Blackboard bb;
    REQUIRE(!bt.propose(bb, tick(0.0)).has_value()); // 首个分支 = idle 叶子
    REQUIRE(bt.current_path() == "root/0");
}

TEST_CASE("BT 确定性：同黑板与时间序列结果一致", "[bt]") {
    auto a = make_tree(acceptance_tree_json());
    auto b = make_tree(acceptance_tree_json());
    Blackboard bb;
    for (double t : {0.0, 0.5, 1.0, 1.5}) {
        REQUIRE(a.propose(bb, tick(t)).has_value());
        REQUIRE(b.propose(bb, tick(t)).has_value());
        REQUIRE(a.current_path() == b.current_path());
    }
}

TEST_CASE("BT 非法定义 fail-fast", "[bt]") {
    BtNode root;
    // 缺 root
    REQUIRE(parse_bt_definition(nlohmann::json::object(), root).has_value());
    // action 缺 intent
    REQUIRE(parse_bt_definition(nlohmann::json::parse(R"({"root": {"kind": "action"}})"), root)
                .has_value());
    // done_when 位于 selector 直接子节点
    REQUIRE(parse_bt_definition(nlohmann::json::parse(R"({
        "root": {"kind": "selector", "children": [
            {"kind": "action", "intent": {"kind": "emote", "name": "x"},
             "done_when": {"elapsed_ge": 1.0}}
        ]}
    })"),
                                root)
                .has_value());
    // 未知 kind
    REQUIRE(parse_bt_definition(nlohmann::json::parse(R"({"root": {"kind": "random"}})"), root)
                .has_value());
    // condition 缺 child
    REQUIRE(parse_bt_definition(nlohmann::json::parse(R"({
        "root": {"kind": "condition", "condition": {"bb": {"a": true}}}
    })"),
                                root)
                .has_value());
    // 空 children
    REQUIRE(parse_bt_definition(
                nlohmann::json::parse(R"({"root": {"kind": "selector", "children": []}})"), root)
                .has_value());
    // done_when 未知键
    REQUIRE(parse_bt_definition(nlohmann::json::parse(R"({
        "root": {"kind": "sequence", "children": [
            {"kind": "action", "intent": {"kind": "emote", "name": "x"},
             "done_when": {"random_key": 1}}
        ]}
    })"),
                                root)
                .has_value());
}
