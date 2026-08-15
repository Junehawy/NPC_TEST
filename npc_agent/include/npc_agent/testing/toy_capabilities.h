// 玩具能力（npc_agent/testing/）：测试与示例共用，演示三类模式——
//  - ToyPatrolDecision（IDecisionMaker）：权威意图源；rng_seed 确定性扰动；
//    alarm 置位时返回 ready=false 演示"等待异步结果 → 模块候选兜底"（RA-§3.4）。
//  - ToyStartleCapability（ICapability）：事件驱动，on_event 置内部状态，
//    propose 一次性消费（内部状态随存档序列化，RA-§7.3）。
//  - ToyGreetCapability（ICapability）：黑板驱动，读感知注入结果产出候选。
#pragma once

#include <optional>
#include <string>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::testing {

class ToyPatrolDecision final : public IDecisionMaker {
public:
    explicit ToyPatrolDecision(Vec3 home = Vec3{}) : home_(home) {}

    std::string_view id() const override { return "toy_patrol"; }

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext& tc) override {
        if (const auto* alarm = bb.get("alarm");
            alarm != nullptr && alarm->is_boolean() && alarm->get<bool>()) {
            // 模拟等待异步结果：ready=false 不参与仲裁，由模块候选兜底（RA-§3.4）
            Intent pending;
            pending.payload = MoveIntent{};
            pending.priority = 1.0f;
            pending.ready = false;
            return pending;
        }
        // 确定性扰动：仅由本 tick rng_seed 派生（RA-§3.7，禁系统随机源）
        const float dx = static_cast<float>(static_cast<int>(tc.rng_seed % 101u) - 50) / 50.0f;
        Intent intent;
        intent.payload = MoveIntent{Vec3{home_.x + dx, home_.y, home_.z}, 1.5f};
        intent.priority = 1.0f;
        return intent;
    }

    void to_json(json& out) const override { out = json{{"home", {home_.x, home_.y, home_.z}}}; }

    void from_json(const json& in) override {
        if (in.is_object() && in.contains("home") && in["home"].is_array() &&
            in["home"].size() == 3) {
            home_ = Vec3{in["home"][0].get<float>(), in["home"][1].get<float>(),
                         in["home"][2].get<float>()};
        }
    }

private:
    Vec3 home_;
};

class ToyStartleCapability final : public ICapability {
public:
    std::string_view id() const override { return "toy_startle"; }

    void on_event(const core::AgentEvent& e) override {
        if (e.type == "stimulus.gunshot")
            startled_ = true;
    }

    std::optional<Intent> propose(const core::Blackboard&, const TickContext&) override {
        if (!startled_)
            return std::nullopt;
        startled_ = false; // 单次消费（玩具语义：无论是否胜出，本 tick 后复位）
        Intent intent;
        intent.payload = EmoteIntent{"startled"};
        intent.priority = 5.0f;
        return intent;
    }

    void to_json(json& out) const override { out = json{{"startled", startled_}}; }

    void from_json(const json& in) override {
        startled_ = in.is_object() ? in.value("startled", false) : false;
    }

private:
    bool startled_ = false;
};

class ToyGreetCapability final : public ICapability {
public:
    std::string_view id() const override { return "toy_greet"; }

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext&) override {
        const auto* seen = bb.get("perceived_entities");
        if (seen == nullptr || !seen->is_array())
            return std::nullopt;
        for (const auto& id : *seen) {
            if (id.is_string() && id.get<std::string>() == "player") {
                Intent intent;
                intent.payload = SayIntent{"你好，旅行者", "friendly"};
                intent.priority = 2.0f;
                return intent;
            }
        }
        return std::nullopt;
    }

    void to_json(json& out) const override { out = json::object(); }

    void from_json(const json&) override {}
};

} // namespace npc_agent::testing
