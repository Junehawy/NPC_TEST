// StartleCapability —— 演示惊吓能力（game_adapter 层，参数化替代 testing::ToyStartleCapability）：
// 收到 stimulus_type 刺激事件后置内部状态，propose 一次性消费为表情意图（单次消费语义
// 同玩具实现：无论是否胜出，本 tick 后复位）。全部行为参数来自 DemoConfig。
// 线程契约：【驱动线程】，与 ICapability 一致。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "demo_config.h"
#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::adapter::godot_demo {

class StartleCapability final : public ICapability {
public:
    explicit StartleCapability(StartleParams params) : params_(std::move(params)) {}

    std::string_view id() const override { return "demo_startle"; }

    void on_event(const core::AgentEvent& e) override {
        if (e.type == "stimulus." + params_.stimulus_type)
            startled_ = true;
    }

    std::optional<Intent> propose(const core::Blackboard&, const TickContext&) override {
        if (!startled_)
            return std::nullopt;
        startled_ = false; // 单次消费（玩具语义：本 tick 后复位）
        Intent intent;
        intent.payload = EmoteIntent{params_.emote};
        intent.priority = params_.priority;
        return intent;
    }

    void to_json(json& out) const override { out = json{{"startled", startled_}}; }

    void from_json(const json& in) override {
        startled_ = in.is_object() ? in.value("startled", false) : false;
    }

private:
    StartleParams params_;
    bool startled_ = false;
};

} // namespace npc_agent::adapter::godot_demo
