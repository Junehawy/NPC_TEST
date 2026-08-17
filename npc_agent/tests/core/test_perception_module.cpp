// 感知模块测试（TS-§3 scenarios 行：订阅/打包/衰减/环容量/权威决策器下仍刷新/序列化）。
#include <catch2/catch_test_macros.hpp>

#include "npc_agent/capabilities/perception_module.h"
#include "npc_agent/core/agent_system.h"
#include "npc_agent/core/blackboard.h"
#include "npc_agent/core/event.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"
#include "npc_agent/testing/toy_capabilities.h"

using namespace npc_agent;
using namespace npc_agent::capabilities;
using namespace npc_agent::core;
using namespace npc_agent::testing;

namespace {

AgentEvent gunshot_event(double game_time = 1.0) {
    AgentEvent e;
    e.type = "stimulus.gunshot";
    e.source = "player";
    e.game_time = game_time;
    e.payload = nlohmann::json{{"position", nlohmann::json::array({5, 0, 0})}};
    return e;
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

TEST_CASE("感知模块：订阅刺激并打包到黑板", "[perception]") {
    PerceptionModule module;
    module.on_event(gunshot_event(1.0));
    Blackboard bb;
    module.on_tick(bb, tick(1.0));
    // 旗标
    REQUIRE(bb.get("heard_gunshot") != nullptr);
    REQUIRE(bb.get("heard_gunshot")->is_boolean());
    REQUIRE(bb.get("heard_gunshot")->get<bool>());
    // 打包视图
    const auto* perception = bb.get("perception");
    REQUIRE(perception != nullptr);
    REQUIRE((*perception)["window_seconds"].get<double>() == 5.0);
    REQUIRE((*perception)["stimuli"].is_array());
    REQUIRE((*perception)["stimuli"].size() == 1);
    const auto& first = (*perception)["stimuli"][0];
    REQUIRE(first["type"] == "gunshot");
    REQUIRE(first["source"] == "player");
    REQUIRE(first["payload"]["position"] == nlohmann::json::array({5, 0, 0}));
}

TEST_CASE("感知模块：非刺激事件不订阅", "[perception]") {
    PerceptionModule module;
    AgentEvent e;
    e.type = "dialogue.line";
    module.on_event(e);
    Blackboard bb;
    module.on_tick(bb, tick(0.0));
    REQUIRE(bb.get("heard_dialogue.line") == nullptr);
    const auto* perception = bb.get("perception");
    REQUIRE(perception != nullptr);
    REQUIRE((*perception)["stimuli"].size() == 0);
}

TEST_CASE("感知模块：旗标随时间窗衰减", "[perception]") {
    PerceptionModuleParams params;
    params.stimulus_window_seconds = 2.0;
    PerceptionModule module(params);
    module.on_event(gunshot_event(1.0));
    Blackboard bb;
    module.on_tick(bb, tick(1.0));
    REQUIRE(bb.get("heard_gunshot")->get<bool>());
    // 2.5s 后仍在窗口内（1.0 + 2.0 = 3.0）
    module.on_tick(bb, tick(2.5));
    REQUIRE(bb.get("heard_gunshot")->get<bool>());
    // 超出窗口 → 复位
    module.on_tick(bb, tick(3.1));
    REQUIRE_FALSE(bb.get("heard_gunshot")->get<bool>());
    REQUIRE((*bb.get("perception"))["stimuli"].size() == 0);
}

TEST_CASE("感知模块：环缓冲容量受限", "[perception]") {
    PerceptionModuleParams params;
    params.max_stimuli = 3;
    PerceptionModule module(params);
    for (double t = 1.0; t <= 5.0; t += 1.0)
        module.on_event(gunshot_event(t));
    Blackboard bb;
    module.on_tick(bb, tick(5.0));
    const auto& stimuli = (*bb.get("perception"))["stimuli"];
    REQUIRE(stimuli.size() == 3);
    REQUIRE(stimuli[0]["game_time"].get<double>() == 3.0); // 最旧两条已挤出
}

TEST_CASE("感知模块：多类型刺激分别置旗", "[perception]") {
    PerceptionModule module;
    module.on_event(gunshot_event(1.0));
    AgentEvent explosion;
    explosion.type = "stimulus.explosion";
    explosion.game_time = 1.5;
    module.on_event(explosion);
    Blackboard bb;
    module.on_tick(bb, tick(1.5));
    REQUIRE(bb.get("heard_gunshot")->get<bool>());
    REQUIRE(bb.get("heard_explosion")->get<bool>());
}

TEST_CASE("感知模块：决策器权威期间 on_tick 仍刷新（R9 钩子）", "[perception]") {
    // 权威决策器（玩具巡逻）+ 感知模块：枪声后旗标仍应写入黑板。
    MockWorld world;
    MockBody body;
    AgentSystem system;
    system.set_current_world(world);
    AgentConfig cfg;
    cfg.id = "guard";
    cfg.perception.radius = 0.0f; // 关闭查询感知，仅刺激
    auto& agent = system.create_agent(std::move(cfg), body);
    agent.set_decision_maker(std::make_unique<ToyPatrolDecision>(Vec3{}));
    agent.register_capability(std::make_unique<PerceptionModule>());

    world.advance(0.1);
    system.inject_stimulus(Stimulus{"gunshot", Vec3{5, 0, 0}, 1.0f, "player"});
    world.advance(0.1);
    system.tick();

    const auto* heard = agent.blackboard().get("heard_gunshot");
    REQUIRE(heard != nullptr);
    REQUIRE(heard->get<bool>());
    // 决策器权威（巡逻 MoveIntent）不受影响
    REQUIRE(agent.last_intent().has_value());
    REQUIRE(std::holds_alternative<MoveIntent>(agent.last_intent()->payload));
}

TEST_CASE("感知模块：序列化随存档恢复", "[perception]") {
    PerceptionModule module;
    module.on_event(gunshot_event(2.0));
    nlohmann::json saved;
    module.to_json(saved);
    REQUIRE(saved["stimuli"].is_array());
    REQUIRE(saved["stimuli"].size() == 1);

    PerceptionModule restored;
    restored.from_json(saved);
    Blackboard bb;
    restored.on_tick(bb, tick(2.0));
    REQUIRE(bb.get("heard_gunshot")->get<bool>());
}

TEST_CASE("感知模块：propose 恒无候选", "[perception]") {
    PerceptionModule module;
    Blackboard bb;
    REQUIRE(!module.propose(bb, tick(0.0)).has_value());
}
