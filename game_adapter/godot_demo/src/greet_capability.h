// GreetCapability —— 演示问候能力（game_adapter 层，参数化替代 testing::ToyGreetCapability）：
// 玩家被感知（黑板 perceived_entities 含 "player"）且距离（黑板 player_distance，
// 由演示节点每 tick 注入）≤ max_distance 时提出问候台词。全部行为参数来自 DemoConfig。
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

class GreetCapability final : public ICapability {
public:
    explicit GreetCapability(GreetParams params) : params_(std::move(params)) {}

    std::string_view id() const override { return "demo_greet"; }

    void on_event(const core::AgentEvent&) override {}

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext&) override {
        if (!params_.enabled)
            return std::nullopt;
        const auto* seen = bb.get("perceived_entities");
        if (seen == nullptr || !seen->is_array())
            return std::nullopt;
        bool player = false;
        for (const auto& id : *seen)
            player = player || (id.is_string() && id.get<std::string>() == "player");
        if (!player)
            return std::nullopt;
        const auto* dist = bb.get("player_distance");
        if (dist != nullptr && dist->is_number() && dist->get<float>() > params_.max_distance)
            return std::nullopt; // 距离超出问候阈值（默认=感知半径）
        Intent intent;
        intent.payload = SayIntent{params_.text, params_.tone};
        intent.priority = params_.priority;
        return intent;
    }

    void to_json(json& out) const override {
        out = json{{"enabled", params_.enabled},
                   {"text", params_.text},
                   {"tone", params_.tone},
                   {"priority", params_.priority},
                   {"max_distance", params_.max_distance}};
    }

    void from_json(const json& in) override {
        if (!in.is_object())
            return;
        params_.enabled = in.value("enabled", params_.enabled);
        params_.text = in.value("text", params_.text);
        params_.tone = in.value("tone", params_.tone);
        params_.priority = in.value("priority", params_.priority);
        params_.max_distance = in.value("max_distance", params_.max_distance);
    }

private:
    GreetParams params_;
};

} // namespace npc_agent::adapter::godot_demo
