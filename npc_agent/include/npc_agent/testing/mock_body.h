// MockBody —— 无头身体：IAgentBody 的测试实现（测试与示例共用）。
// 行为：记录全部动作；move_to 立即到达（简化，动作生命周期回投见阶段 2）。
// 线程契约：【驱动线程】（与接口一致）。
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "npc_agent/interfaces/i_agent_body.h"

namespace npc_agent::testing {

// 一次动作记录（断言用）。
struct BodyAction {
    std::string kind; // "move_to" / "emote" / "say" / "game_event"
    json payload;
};

class MockBody final : public IAgentBody {
public:
    explicit MockBody(BodyState initial = BodyState{}) : state_(std::move(initial)) {}

    BodyState body_state() const override { return state_; }

    ActionHandle move_to(Vec3 target, float speed) override {
        state_.position = target; // 简化：立即到达
        return record("move_to",
                      json{{"target", {target.x, target.y, target.z}}, {"speed", speed}});
    }

    ActionHandle play_emote(const std::string& name) override {
        return record("emote", json{{"name", name}});
    }

    ActionHandle say(const DialogueLine& line) override {
        return record("say", json{{"text", line.text}, {"tone", line.tone}});
    }

    ActionHandle dispatch_game_event(const GameEvent& e) override {
        return record("game_event", json{{"type", e.type}, {"payload", e.payload}});
    }

    // 观察与测试驱动。
    const std::vector<BodyAction>& actions() const { return actions_; }
    void clear_actions() { actions_.clear(); }
    void set_position(Vec3 p) { state_.position = p; }

private:
    ActionHandle record(std::string kind, json payload) {
        actions_.push_back(BodyAction{std::move(kind), std::move(payload)});
        return ActionHandle{next_handle_++};
    }

    BodyState state_;
    uint64_t next_handle_ = 1;
    std::vector<BodyAction> actions_;
};

} // namespace npc_agent::testing
