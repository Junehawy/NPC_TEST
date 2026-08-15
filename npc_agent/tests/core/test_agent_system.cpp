// AgentSystem 场景验收（RA 阶段 1 验收项 + R5-3 感知注入 + 读档恢复）。
#include <memory>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "npc_agent/core/agent_system.h"
#include "npc_agent/core/capability_factory.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"
#include "npc_agent/testing/toy_capabilities.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::testing;

namespace {

// 装配一个守卫：巡逻决策器 + 受惊 + 问候。
void assemble(AgentSystem& sys, AgentConfig cfg, MockBody& body) {
    auto& agent = sys.create_agent(std::move(cfg), body);
    agent.set_decision_maker(std::make_unique<ToyPatrolDecision>(Vec3{0, 0, 0}));
    agent.register_capability(std::make_unique<ToyGreetCapability>());
    agent.register_capability(std::make_unique<ToyStartleCapability>());
}

AgentConfig guard_config(std::string id = "guard_1", uint64_t seed = 42) {
    AgentConfig c;
    c.id = std::move(id);
    c.rng_seed = seed;
    c.perception.radius = 10.0f;
    return c;
}

CapabilityFactory make_factory() {
    CapabilityFactory factory;
    factory.register_creator("toy_patrol", [] { return std::make_unique<ToyPatrolDecision>(); });
    factory.register_creator("toy_startle",
                             [] { return std::make_unique<ToyStartleCapability>(); });
    factory.register_creator("toy_greet", [] { return std::make_unique<ToyGreetCapability>(); });
    return factory;
}

} // namespace

TEST_CASE("[agent_system] 验收场景_刺激→事件→仲裁→意图执行到 MockWorld") {
    MockWorld world;
    world.add_entity("player", Vec3{8, 0, 0});
    MockBody body;
    body.set_position(Vec3{0, 0, 0});

    AgentSystem sys;
    sys.set_current_world(world);
    assemble(sys, guard_config(), body);

    world.advance(0.1);
    sys.tick();

    // 感知注入（R5-3）：AgentSystem 代执行 IWorld::sense 并写入黑板
    const auto* seen = sys.find_agent("guard_1")->blackboard().get("perceived_entities");
    REQUIRE(seen != nullptr);
    REQUIRE(seen->is_array());
    REQUIRE(seen->size() == 1);
    REQUIRE((*seen)[0].get<std::string>() == "player");

    // 决策器权威 → 移动意图执行到身体
    const auto& intent = sys.find_agent("guard_1")->last_intent();
    REQUIRE(intent.has_value());
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload));
    REQUIRE(body.actions().size() == 1);
    REQUIRE(body.actions()[0].kind == "move_to");

    // 快照组装：世界时间一致；快照先于执行 → 记录旧位置
    const auto& snap = sys.find_agent("guard_1")->last_snapshot();
    REQUIRE(snap.world.game_time == world.tick_context().game_time);
    REQUIRE(snap.self.position.x == 0.0f);

    // 枪声刺激 → 全局事件广播（下个 tick 派发）；决策器仍权威
    sys.inject_stimulus(Stimulus{"gunshot", Vec3{5, 0, 0}, 1.0f, "player"});
    REQUIRE(world.stimuli_log().size() == 1);
    world.advance(0.1);
    sys.tick();
    REQUIRE(std::holds_alternative<MoveIntent>(sys.find_agent("guard_1")->last_intent()->payload));
    REQUIRE(body.actions().size() == 2);
}

TEST_CASE("[agent_system] 全局广播_多 agent 同时接收") {
    MockWorld world;
    MockBody body_a;
    MockBody body_b;
    AgentSystem sys;
    sys.set_current_world(world);
    auto& a = sys.create_agent(guard_config("a", 1), body_a);
    auto& b = sys.create_agent(guard_config("b", 2), body_b);
    a.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    b.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    a.register_capability(std::make_unique<ToyStartleCapability>());
    b.register_capability(std::make_unique<ToyStartleCapability>());

    // 决策器 pending（alarm）→ 枪声后两个 agent 都兜底产出 startled
    a.blackboard().set("alarm", true);
    b.blackboard().set("alarm", true);
    sys.broadcast(AgentEvent{"stimulus.gunshot", "player", nlohmann::json::object(), 0.0});
    world.advance(0.1);
    sys.tick();

    REQUIRE(std::holds_alternative<EmoteIntent>(a.last_intent()->payload));
    REQUIRE(std::holds_alternative<EmoteIntent>(b.last_intent()->payload));
}

TEST_CASE("[agent_system] 私有事件_仅本 agent 接收") {
    MockWorld world;
    MockBody body_a;
    MockBody body_b;
    AgentSystem sys;
    sys.set_current_world(world);
    auto& a = sys.create_agent(guard_config("a", 1), body_a);
    auto& b = sys.create_agent(guard_config("b", 2), body_b);
    a.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    b.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    a.register_capability(std::make_unique<ToyStartleCapability>());
    b.register_capability(std::make_unique<ToyStartleCapability>());
    a.blackboard().set("alarm", true); // 仅 a 的决策器 pending

    a.enqueue_private(AgentEvent{"stimulus.gunshot", "player", nlohmann::json::object(), 0.0});
    world.advance(0.1);
    sys.tick();

    REQUIRE(std::holds_alternative<EmoteIntent>(a.last_intent()->payload)); // a 兜底 startled
    REQUIRE(std::holds_alternative<MoveIntent>(b.last_intent()->payload));  // b 不受影响
}

TEST_CASE("[agent_system] 读档恢复_黑板/能力状态/RNG 一致") {
    MockWorld world;
    world.add_entity("player", Vec3{8, 0, 0});
    MockBody body1;
    body1.set_position(Vec3{0, 0, 0});

    AgentSystem sys1;
    sys1.set_current_world(world);
    assemble(sys1, guard_config(), body1);
    world.advance(0.1);
    sys1.tick();
    sys1.tick(); // 根 RNG 前进

    const nlohmann::json saved = sys1.to_json();

    // 恢复：工厂重建能力；world 与身体由调用方重挂
    AgentSystem sys2;
    CapabilityFactory factory = make_factory();
    sys2.set_capability_factory(&factory);
    std::string error;
    REQUIRE(AgentSystem::restore(saved, sys2, error));
    sys2.set_current_world(world);
    MockBody body2;
    body2.set_position(Vec3{0, 0, 0});
    sys2.find_agent("guard_1")->attach_body(body2);

    // 黑板一致
    REQUIRE(sys2.find_agent("guard_1")->blackboard().entries() ==
            sys1.find_agent("guard_1")->blackboard().entries());

    // 后续意图序列一致（根 RNG 状态恢复验证，RA-§3.7）
    for (int i = 0; i < 3; ++i) {
        world.advance(0.1);
        sys1.tick();
        sys2.tick();
        const auto& i1 = sys1.find_agent("guard_1")->last_intent();
        const auto& i2 = sys2.find_agent("guard_1")->last_intent();
        REQUIRE(i1.has_value());
        REQUIRE(i2.has_value());
        const auto& m1 = std::get<MoveIntent>(i1->payload);
        const auto& m2 = std::get<MoveIntent>(i2->payload);
        REQUIRE(m1.target.x == m2.target.x);
        REQUIRE(m1.target.y == m2.target.y);
    }
}

TEST_CASE("[agent_system] 存档恢复失败路径_未知能力 id → 报错定位") {
    MockWorld world;
    MockBody body;
    AgentSystem sys;
    sys.set_current_world(world);
    assemble(sys, guard_config(), body);
    const nlohmann::json saved = sys.to_json();

    CapabilityFactory empty_factory; // 未注册任何能力
    AgentSystem sys2;
    sys2.set_capability_factory(&empty_factory);
    std::string error;
    REQUIRE_FALSE(AgentSystem::restore(saved, sys2, error));
    // 恢复按存档顺序重建：第一个缺失的是 toy_greet（capabilities 先于 decision_maker）
    REQUIRE(error.find("toy_greet") != std::string::npos);
    REQUIRE(error.find("未在工厂注册") != std::string::npos);
}
