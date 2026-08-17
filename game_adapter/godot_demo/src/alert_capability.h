// AlertCapability —— 演示警戒能力（game_adapter 层）：alarm 期间持续提出警戒表情，
// 构成优先级梯次（默认 惊吓5 > 问候2 > 警戒1.5）——枪声后先惊吓、玩家在场则交谈、
// 玩家远离则保持警戒，巡逻决策器（pending）恢复前由本能力兜底。
// 全部行为参数来自 DemoConfig。
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

class AlertCapability final : public ICapability {
public:
    explicit AlertCapability(AlertParams params) : params_(std::move(params)) {}

    std::string_view id() const override { return "demo_alert"; }

    void on_event(const core::AgentEvent&) override {}

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext&) override {
        const auto* alarm = bb.get("alarm");
        if (alarm == nullptr || !alarm->is_boolean() || !alarm->get<bool>())
            return std::nullopt;
        Intent intent;
        intent.payload = EmoteIntent{params_.emote};
        intent.priority = params_.priority;
        return intent;
    }

    void to_json(json& out) const override { out = json::object(); }

    void from_json(const json&) override {}

private:
    AlertParams params_;
};

} // namespace npc_agent::adapter::godot_demo
