#include "npc_agent/testing/acceptance_scenario.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "npc_agent/capabilities/perception_module.h"
#include "npc_agent/core/agent_config.h"
#include "npc_agent/decision/fsm_decision_maker.h"
#include "npc_agent/interfaces/i_world.h"

namespace npc_agent::testing {

std::string AcceptanceScenario::fsm_definition_json() {
    // idle →(heard_gunshot)→ alert →(1.0s)→ call_support →(1.5s)→ search →(2.0s)→ idle
    return R"({
        "initial": "idle",
        "states": [
            {
                "name": "idle",
                "intent": {"kind": "idle"},
                "transitions": [
                    {"target": "alert", "condition": {"bb": {"heard_gunshot": true}}}
                ]
            },
            {
                "name": "alert",
                "priority": 5.0,
                "intent": {"kind": "emote", "name": "startled"},
                "transitions": [
                    {"target": "call_support", "condition": {"elapsed_ge": 1.0}}
                ]
            },
            {
                "name": "call_support",
                "priority": 4.0,
                "intent": {"kind": "say", "text": "呼叫支援！请求增援", "tone": "urgent"},
                "transitions": [
                    {"target": "search", "condition": {"elapsed_ge": 1.5}}
                ]
            },
            {
                "name": "search",
                "priority": 3.0,
                "intent": {"kind": "move_to", "target": [5, 0, 0], "speed": 1.5},
                "transitions": [
                    {"target": "idle", "condition": {"elapsed_ge": 2.0}}
                ]
            }
        ]
    })";
}

AcceptanceScenario::AcceptanceScenario(uint64_t seed) {
    system_.set_current_world(world_);

    core::AgentConfig cfg;
    cfg.id = kAgentId;
    cfg.decision_kind = "fsm";
    cfg.rng_seed = seed;
    cfg.perception.radius = 0.0f; // 验收链路为刺激驱动（听觉），关闭查询感知

    agent_ = &system_.create_agent(std::move(cfg), body_);

    decision::FsmDefinition fsm_def;
    // 非抛解析（CS-§9：库代码不依赖异常，保持可移植到 -fno-exceptions 宿主）。
    const nlohmann::json fsm_json = nlohmann::json::parse(fsm_definition_json(), nullptr, false);
    if (fsm_json.is_discarded())
        std::abort(); // 定义内嵌且经测试锁定，解析失败属编程错误
    if (auto err = decision::parse_fsm_definition(fsm_json, fsm_def); err.has_value())
        std::abort(); // 同上
    agent_->set_decision_maker(std::make_unique<decision::FsmDecisionMaker>(std::move(fsm_def)));
    agent_->register_capability(std::make_unique<capabilities::PerceptionModule>());
}

void AcceptanceScenario::run(std::size_t ticks, double dt) {
    for (std::size_t i = 0; i < ticks; ++i) {
        if (i == kGunshotTick) {
            // 确定性刺激脚本（R8：按 tick 序号对齐）；位置信息走 payload 约定。
            Stimulus gunshot;
            gunshot.type = "gunshot";
            gunshot.position = Vec3{5.0f, 0.0f, 0.0f};
            gunshot.magnitude = 1.0f;
            gunshot.source_id = "player";
            gunshot.payload = nlohmann::json{{"position", nlohmann::json::array({5, 0, 0})}};
            system_.inject_stimulus(gunshot);
        }
        // 先 tick 后 advance：本 tick 的 tick_index == 循环下标 i（语义对齐，R8 按 tick 对齐）。
        system_.tick();
        world_.advance(dt);
    }
}

core::AgentSystem& AcceptanceScenario::system() {
    return system_;
}

core::Agent& AcceptanceScenario::agent() {
    return *agent_;
}

MockWorld& AcceptanceScenario::world() {
    return world_;
}

} // namespace npc_agent::testing
