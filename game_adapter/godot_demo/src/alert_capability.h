// AlertCapability —— 演示警戒能力（game_adapter 层）：alarm 期间持续提出"警戒"表情，
// 构成优先级梯次：惊吓(5) > 问候(2) > 警戒(1.5)——枪声后先惊吓、玩家在场则交谈、
// 玩家远离则保持警戒，巡逻决策器（pending）恢复前由本能力兜底。
// 线程契约：【驱动线程】，与 ICapability 一致。
#pragma once

#include <optional>
#include <string_view>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::adapter::godot_demo {

class AlertCapability final : public ICapability {
public:
    std::string_view id() const override { return "demo_alert"; }

    void on_event(const core::AgentEvent&) override {}

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext&) override {
        const auto* alarm = bb.get("alarm");
        if (alarm == nullptr || !alarm->is_boolean() || !alarm->get<bool>())
            return std::nullopt;
        Intent intent;
        intent.payload = EmoteIntent{"警戒"};
        intent.priority = 1.5f;
        return intent;
    }

    void to_json(json& out) const override { out = json::object(); }

    void from_json(const json&) override {}
};

} // namespace npc_agent::adapter::godot_demo
