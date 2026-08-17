// 动作生命周期回投测试（TS-§3 scenarios 行：完成/失败/取消事件 → 决策器与能力模块）。
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "npc_agent/core/agent.h"
#include "npc_agent/core/agent_system.h"
#include "npc_agent/core/blackboard.h"
#include "npc_agent/core/event.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"
#include "npc_agent/testing/toy_capabilities.h"

using namespace npc_agent;
using namespace npc_agent::core;
using namespace npc_agent::testing;

namespace {

// 记录收到的全部事件（断言用）。
class RecordingCapability final : public ICapability {
public:
    std::string_view id() const override { return "recording"; }

    std::optional<Intent> propose(const Blackboard&, const TickContext&) override {
        return std::nullopt;
    }

    void on_event(const AgentEvent& e) override { events_.push_back(e); }

    void to_json(nlohmann::json& out) const override { out = nlohmann::json::object(); }

    void from_json(const nlohmann::json&) override {}

    const std::vector<AgentEvent>& events() const { return events_; }

private:
    std::vector<AgentEvent> events_;
};

} // namespace

TEST_CASE("动作完成回投：下个 tick 派发 action.completed", "[action_lifecycle]") {
    MockWorld world;
    MockBody body;
    AgentSystem system;
    system.set_current_world(world);
    AgentConfig cfg;
    cfg.id = "guard";
    cfg.perception.radius = 0.0f;
    auto& agent = system.create_agent(std::move(cfg), body);
    auto recorder = std::make_unique<RecordingCapability>();
    auto* recording = recorder.get();
    agent.register_capability(std::move(recorder));

    const ActionHandle handle = body.move_to(Vec3{1, 0, 0}, 1.0f);
    agent.report_action_result(handle, "completed");

    world.advance(0.1);
    system.tick(); // 派发

    REQUIRE(recording->events().size() == 1);
    REQUIRE(recording->events()[0].type == "action.completed");
    REQUIRE(recording->events()[0].source == "guard");
    REQUIRE(recording->events()[0].payload["handle"] == handle.id);
    REQUIRE(recording->events()[0].game_time == 0.1);
}

TEST_CASE("动作失败/取消回投：action.failed / action.cancelled", "[action_lifecycle]") {
    MockWorld world;
    MockBody body;
    AgentSystem system;
    system.set_current_world(world);
    AgentConfig cfg;
    cfg.id = "guard";
    cfg.perception.radius = 0.0f;
    auto& agent = system.create_agent(std::move(cfg), body);
    auto recorder = std::make_unique<RecordingCapability>();
    auto* recording = recorder.get();
    agent.register_capability(std::move(recorder));

    const ActionHandle h1 = body.move_to(Vec3{1, 0, 0}, 1.0f);
    const ActionHandle h2 = body.move_to(Vec3{2, 0, 0}, 1.0f);
    agent.report_action_result(h1, "failed");
    agent.report_action_result(h2, "cancelled");

    world.advance(0.1);
    system.tick();

    REQUIRE(recording->events().size() == 2);
    REQUIRE(recording->events()[0].type == "action.failed");
    REQUIRE(recording->events()[0].payload["handle"] == h1.id);
    REQUIRE(recording->events()[1].type == "action.cancelled");
    REQUIRE(recording->events()[1].payload["handle"] == h2.id);
}

TEST_CASE("动作回投事件也送达决策器（on_event）", "[action_lifecycle]") {
    // ToyStartleCapability 记录事件不完整；这里用 FSM 决策器（IDecisionMaker 继承
    // ICapability::on_event 默认忽略）验证机制用自定义决策器。
    class RecordingDecision final : public IDecisionMaker {
    public:
        std::string_view id() const override { return "recording_dm"; }
        std::optional<Intent> propose(const Blackboard&, const TickContext&) override {
            return std::nullopt;
        }
        void on_event(const AgentEvent& e) override { events_.push_back(e); }
        void to_json(json& out) const override { out = json::object(); }
        void from_json(const json&) override {}
        const std::vector<AgentEvent>& events() const { return events_; }

    private:
        std::vector<AgentEvent> events_;
    };

    MockWorld world;
    MockBody body;
    AgentSystem system;
    system.set_current_world(world);
    AgentConfig cfg;
    cfg.id = "guard";
    cfg.perception.radius = 0.0f;
    auto& agent = system.create_agent(std::move(cfg), body);
    auto dm = std::make_unique<RecordingDecision>();
    auto* recording_dm = dm.get();
    agent.set_decision_maker(std::move(dm));

    agent.report_action_result(ActionHandle{7}, "completed");
    world.advance(0.1);
    system.tick();

    REQUIRE(recording_dm->events().size() == 1);
    REQUIRE(recording_dm->events()[0].type == "action.completed");
}

TEST_CASE("非法结果值被忽略（编程错误防线）", "[action_lifecycle]") {
    MockWorld world;
    MockBody body;
    AgentSystem system;
    system.set_current_world(world);
    AgentConfig cfg;
    cfg.id = "guard";
    cfg.perception.radius = 0.0f;
    auto& agent = system.create_agent(std::move(cfg), body);
    auto recorder = std::make_unique<RecordingCapability>();
    auto* recording = recorder.get();
    agent.register_capability(std::move(recorder));

    agent.report_action_result(ActionHandle{1}, "exploded");  // 非法
    agent.report_action_result(ActionHandle{2}, "completed"); // 合法
    world.advance(0.1);
    system.tick();

    REQUIRE(recording->events().size() == 1);
    REQUIRE(recording->events()[0].type == "action.completed");
}

TEST_CASE("动作回投跨 tick：多次回报按顺序派发", "[action_lifecycle]") {
    MockWorld world;
    MockBody body;
    AgentSystem system;
    system.set_current_world(world);
    AgentConfig cfg;
    cfg.id = "guard";
    cfg.perception.radius = 0.0f;
    auto& agent = system.create_agent(std::move(cfg), body);
    auto recorder = std::make_unique<RecordingCapability>();
    auto* recording = recorder.get();
    agent.register_capability(std::move(recorder));

    agent.report_action_result(ActionHandle{1}, "completed");
    world.advance(0.1);
    system.tick();
    REQUIRE(recording->events().size() == 1);

    agent.report_action_result(ActionHandle{2}, "completed");
    agent.report_action_result(ActionHandle{3}, "failed");
    world.advance(0.1);
    system.tick();
    REQUIRE(recording->events().size() == 3);
    REQUIRE(recording->events()[1].payload["handle"] == 2);
    REQUIRE(recording->events()[2].type == "action.failed");
    REQUIRE(recording->events()[2].game_time == 0.2);
}
