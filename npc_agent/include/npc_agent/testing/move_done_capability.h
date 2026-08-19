// MoveDoneCapability —— 动作完成旗标能力（npc_agent/testing/，阶段 3 验收第 3 条）：
// 把 action.completed 事件转为黑板单 tick 脉冲旗标 move_done=true，供 FSM/BT
// 条件迁移消费（{"bb":{"move_done":true}}）——演示"动作完成事件正确推进 FSM"链路：
//   宿主报告动作完成 → on_event 记录 → 下个 tick on_tick 置位 → 决策器条件迁移。
// 单 tick 脉冲语义：置位后的下一个 on_tick 自动复位（电平条件不会永久卡真）。
// 线程契约：全部方法【驱动线程】。
#pragma once

#include <optional>
#include <string_view>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/core/event.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::testing {

class MoveDoneCapability final : public ICapability {
public:
    std::string_view id() const override { return "move_done"; }

    void on_event(const core::AgentEvent& e) override {
        if (e.type == "action.completed")
            pending_ = true;
    }

    void on_tick(core::Blackboard& bb, const TickContext&) override {
        // 单 tick 脉冲：事件后的首个 tick 置 true，随后复位。
        bb.set("move_done", pending_);
        pending_ = false;
    }

    std::optional<Intent> propose(const core::Blackboard&, const TickContext&) override {
        return std::nullopt;
    }

    void to_json(nlohmann::json& out) const override {
        out = nlohmann::json{{"pending", pending_}};
    }

    void from_json(const nlohmann::json& in) override {
        pending_ = in.is_object() ? in.value("pending", false) : false;
    }

private:
    bool pending_ = false;
};

} // namespace npc_agent::testing
