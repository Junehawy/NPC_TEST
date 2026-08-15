// 仲裁管线测试（RA-§3.4 四步 + R4-1/R4-3 定案）：
// 决策器权威 → ready=false 兜底 → priority 降序 → 同分按注册序 → 无候选 → 确定性。
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "npc_agent/core/agent.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/toy_capabilities.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::testing;

namespace {

AgentConfig make_config(std::string id, uint64_t seed = 7) {
    AgentConfig c;
    c.id = std::move(id);
    c.rng_seed = seed;
    return c;
}

// 固定候选能力：指定 priority 与 payload（同分/优先序测试用）。
class FixedCap final : public ICapability {
public:
    FixedCap(std::string id, float priority, IntentPayload payload)
        : id_(std::move(id)), priority_(priority), payload_(std::move(payload)) {}

    std::string_view id() const override { return id_; }

    std::optional<Intent> propose(const core::Blackboard&, const TickContext&) override {
        Intent i;
        i.payload = payload_;
        i.priority = priority_;
        return i;
    }

    void to_json(nlohmann::json& out) const override { out = nlohmann::json::object(); }
    void from_json(const nlohmann::json&) override {}

private:
    std::string id_;
    float priority_;
    IntentPayload payload_;
};

std::vector<AgentEvent> gunshot_events() {
    AgentEvent e;
    e.type = "stimulus.gunshot";
    return {e};
}

} // namespace

TEST_CASE("[arbitration] 决策器权威意图_压过更高优先级的模块候选") {
    MockBody body;
    Agent agent(make_config("a"));
    agent.attach_body(body);
    agent.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    agent.register_capability(std::make_unique<ToyStartleCapability>());

    const auto intent = agent.tick(TickContext{}, gunshot_events());
    REQUIRE(intent.has_value());
    REQUIRE(intent->ready);
    // 巡逻（决策器）胜出，而非 startled（priority 5）
    REQUIRE(std::holds_alternative<MoveIntent>(intent->payload));
}

TEST_CASE("[arbitration] 决策器 ready=false_等待异步 → 模块候选兜底") {
    MockBody body;
    Agent agent(make_config("a"));
    agent.attach_body(body);
    agent.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    agent.register_capability(std::make_unique<ToyStartleCapability>());
    agent.blackboard().set("alarm", true); // 决策器进入 pending

    const auto intent = agent.tick(TickContext{}, gunshot_events());
    REQUIRE(intent.has_value());
    REQUIRE(intent->ready);
    REQUIRE(std::holds_alternative<EmoteIntent>(intent->payload)); // 兜底为 startled
}

TEST_CASE("[arbitration] 模块候选_priority 降序 → 高优先胜出") {
    MockBody body;
    Agent agent(make_config("a"));
    agent.attach_body(body);
    agent.register_capability(std::make_unique<FixedCap>("low", 2.0f, EmoteIntent{"low"}));
    agent.register_capability(std::make_unique<FixedCap>("high", 4.0f, EmoteIntent{"high"}));

    const auto intent = agent.tick(TickContext{}, {});
    REQUIRE(intent.has_value());
    REQUIRE(std::get<EmoteIntent>(intent->payload).name == "high");
}

TEST_CASE("[arbitration] 同分_先注册者胜 → 注册序由框架分配") {
    MockBody body;
    Agent agent(make_config("a"));
    agent.attach_body(body);
    agent.register_capability(std::make_unique<FixedCap>("first", 3.0f, EmoteIntent{"first"}));
    agent.register_capability(std::make_unique<FixedCap>("second", 3.0f, EmoteIntent{"second"}));

    const auto intent = agent.tick(TickContext{}, {});
    REQUIRE(intent.has_value());
    REQUIRE(std::get<EmoteIntent>(intent->payload).name == "first");
}

TEST_CASE("[arbitration] 全部无候选 → 无意图") {
    MockBody body;
    Agent agent(make_config("a"));
    agent.attach_body(body);

    const auto intent = agent.tick(TickContext{}, {});
    REQUIRE_FALSE(intent.has_value());
    REQUIRE_FALSE(agent.last_intent().has_value());
}

TEST_CASE("[arbitration] 确定性_同种子同事件序列 → 意图序列一致") {
    MockBody body_a;
    MockBody body_b;
    Agent a(make_config("a", 42));
    Agent b(make_config("b", 42));
    a.attach_body(body_a);
    b.attach_body(body_b);
    a.set_decision_maker(std::make_unique<ToyPatrolDecision>());
    b.set_decision_maker(std::make_unique<ToyPatrolDecision>());

    for (int tick = 0; tick < 10; ++tick) {
        std::vector<AgentEvent> events;
        if (tick == 3)
            events = gunshot_events(); // 同事件序列（RA-§3.7 确定性契约）

        TickContext tc;
        tc.tick_index = static_cast<uint64_t>(tick);
        tc.game_time = 0.1 * static_cast<double>(tick);

        const auto ia = a.tick(tc, events);
        const auto ib = b.tick(tc, events);
        REQUIRE(ia.has_value());
        REQUIRE(ib.has_value());
        const auto& ma = std::get<MoveIntent>(ia->payload);
        const auto& mb = std::get<MoveIntent>(ib->payload);
        REQUIRE(ma.target.x == mb.target.x);
        REQUIRE(ma.target.y == mb.target.y);
        REQUIRE(ma.target.z == mb.target.z);
    }
}
