// 阶段 3 headless 验收（TS-§3 scenarios 行，RA 路线图 3.x 验收）：
// ① NPC 沿 A* 路径移动（find_path 消费）；② 途中障碍触发重新规划；
// ③ 动作完成事件（action.completed → move_done 脉冲）正确推进 FSM。
// 宿主循环（测试驱动）：每 tick → system.tick → 若意图为移动则经 GridNav 求路径 →
// 模拟到达（MockBody 瞬移）→ report_action_result → FSM 经 move_done 推进。
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "npc_agent/core/agent_system.h"
#include "npc_agent/decision/fsm_decision_maker.h"
#include "npc_agent/testing/grid_nav.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"
#include "npc_agent/testing/move_done_capability.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::decision;
using namespace npc_agent::testing;

namespace {

// 导航 FSM：idle →(go)→ to_a(9,5) →(move_done)→ to_b(17,5) →(move_done)→ done。
nlohmann::json nav_fsm_json() {
    return nlohmann::json::parse(R"({
        "initial": "idle",
        "states": [
            {
                "name": "idle",
                "intent": {"kind": "idle"},
                "transitions": [
                    {"target": "to_a", "condition": {"bb": {"go": true}}}
                ]
            },
            {
                "name": "to_a",
                "intent": {"kind": "move_to", "target": [9, 5, 0], "speed": 1.0},
                "transitions": [
                    {"target": "to_b", "condition": {"bb": {"move_done": true}}}
                ]
            },
            {
                "name": "to_b",
                "intent": {"kind": "move_to", "target": [17, 5, 0], "speed": 1.0},
                "transitions": [
                    {"target": "done", "condition": {"bb": {"move_done": true}}}
                ]
            },
            {
                "name": "done",
                "intent": {"kind": "idle"},
                "transitions": []
            }
        ]
    })");
}

struct Fixture {
    MockWorld world;
    MockBody body;
    AgentSystem system;
    GridNav grid{20, 10, 1.0f};
    Agent* agent = nullptr;
    Vec3 last_target_{0.0f, 0.0f, 0.0f}; // MockBody 瞬移：上一目标即当前身体位置

    Fixture() {
        system.set_current_world(world);
        AgentConfig cfg;
        cfg.id = "walker";
        cfg.perception.radius = 0.0f;
        agent = &system.create_agent(std::move(cfg), body);
        FsmDefinition def;
        REQUIRE(!parse_fsm_definition(nav_fsm_json(), def).has_value());
        agent->set_decision_maker(std::make_unique<FsmDecisionMaker>(std::move(def)));
        agent->register_capability(std::make_unique<MoveDoneCapability>());
        // 起点 (1,5) → 甲点 (9,5) 直线上的障碍墙 (4..7,5)：必须绕行。
        for (int c = 4; c <= 7; ++c)
            grid.set_obstacle(c, 5, true);
    }

    // 单 tick 宿主驱动：tick（执行移动，MockBody 瞬移）→ 若为移动意图则以
    // "上一目标"作为当前身体位置求路径 → 报告完成。返回本 tick 求得的路径。
    std::vector<Vec3> host_tick() {
        world.advance(0.1);
        system.tick();
        const auto& intent = agent->last_intent();
        if (!intent.has_value() || !std::holds_alternative<MoveIntent>(intent->payload))
            return {};
        const auto& move = std::get<MoveIntent>(intent->payload);
        const auto path = grid.find_path(last_target_, move.target);
        last_target_ = move.target;
        agent->report_action_result(ActionHandle{1}, "completed"); // 模拟"沿路径走完到达"
        return path;
    }

    // 记录移动意图的目标 x 序列（判断 FSM 推进）。
    std::vector<float> move_target_history() {
        std::vector<float> targets;
        for (int i = 0; i < 40; ++i) {
            host_tick();
            if (agent->last_intent().has_value()) {
                const auto& intent = *agent->last_intent();
                if (std::holds_alternative<MoveIntent>(intent.payload))
                    targets.push_back(std::get<MoveIntent>(intent.payload).target.x);
            }
        }
        return targets;
    }
};

// Vec3 为纯 POD（无 operator==），路径比较用字段级。
bool same_pos(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool paths_differ(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() != b.size())
        return true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same_pos(a[i], b[i]))
            return true;
    }
    return false;
}
} // namespace

TEST_CASE("阶段3：沿 A* 路径移动且绕开障碍（find_path 消费）", "[stage3_nav]") {
    Fixture fx;
    fx.agent->blackboard().set("go", true);
    std::vector<Vec3> all_paths;
    std::vector<float> targets;
    for (int i = 0; i < 40; ++i) {
        const auto path = fx.host_tick();
        if (!path.empty())
            all_paths.insert(all_paths.end(), path.begin(), path.end());
        if (fx.agent->last_intent().has_value()) {
            const auto& intent = *fx.agent->last_intent();
            if (std::holds_alternative<MoveIntent>(intent.payload))
                targets.push_back(std::get<MoveIntent>(intent.payload).target.x);
        }
    }
    REQUIRE(!all_paths.empty());
    // 全部航点不落入阻塞墙 (4..7,5)
    for (const auto& wp : all_paths) {
        const auto cell = fx.grid.world_to_cell(wp);
        REQUIRE(cell.has_value());
        REQUIRE_FALSE(fx.grid.is_blocked(cell->first, cell->second));
    }
    // FSM 推进：甲点 (9,5) 与乙点 (17,5) 两个移动阶段均出现
    REQUIRE(std::find(targets.begin(), targets.end(), 9.0f) != targets.end());
    REQUIRE(std::find(targets.begin(), targets.end(), 17.0f) != targets.end());
}

TEST_CASE("阶段3：途中障碍触发重新规划（find_path 结果随障碍变化）", "[stage3_nav]") {
    Fixture fx;
    fx.agent->blackboard().set("go", true);
    std::vector<Vec3> path_before;
    for (int i = 0; i < 20 && path_before.empty(); ++i)
        path_before = fx.host_tick();
    REQUIRE(!path_before.empty());

    // 堵死原绕行通道上方 (3..8,4) → 必须重新规划改道。
    for (int c = 3; c <= 8; ++c)
        fx.grid.set_obstacle(c, 4, true);
    const std::uint64_t version_after = fx.grid.obstacle_version();

    std::vector<Vec3> path_after;
    for (int i = 0; i < 20 && path_after.empty(); ++i)
        path_after = fx.host_tick();
    REQUIRE(!path_after.empty());
    REQUIRE(paths_differ(path_after, path_before)); // 重新规划生效
    REQUIRE(version_after > 0);
    // 新路径航点全部未阻塞
    for (const auto& wp : path_after) {
        const auto cell = fx.grid.world_to_cell(wp);
        REQUIRE(cell.has_value());
        REQUIRE_FALSE(fx.grid.is_blocked(cell->first, cell->second));
    }
}

TEST_CASE("阶段3：动作完成事件正确推进 FSM（move_done 脉冲）", "[stage3_nav]") {
    Fixture fx;
    fx.agent->blackboard().set("go", true);
    const auto targets = fx.move_target_history();
    // 移动完成 → move_done → FSM 从 to_a(9) 推进到 to_b(17)（B 出现即证明链路生效）
    REQUIRE(std::find(targets.begin(), targets.end(), 17.0f) != targets.end());
    // 完成后回到 idle（done 状态无移动意图）
    bool reached_idle = false;
    for (int i = 0; i < 10; ++i) {
        fx.host_tick();
        if (!fx.agent->last_intent().has_value())
            reached_idle = true;
    }
    REQUIRE(reached_idle);
}

TEST_CASE("阶段3：move_done 单 tick 脉冲（事件后一 tick true，随后复位）", "[stage3_nav]") {
    MoveDoneCapability cap;
    Blackboard bb;
    TickContext tc;
    tc.game_time = 0.0;
    core::AgentEvent e;
    e.type = "action.completed";
    e.game_time = 0.0;
    cap.on_event(e);
    cap.on_tick(bb, tc);
    REQUIRE(bb.get("move_done")->get<bool>());
    tc.game_time = 0.1;
    cap.on_tick(bb, tc); // 下个 tick 复位
    REQUIRE_FALSE(bb.get("move_done")->get<bool>());
}
