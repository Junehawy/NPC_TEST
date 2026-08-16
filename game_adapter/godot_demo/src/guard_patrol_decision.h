// GuardPatrolDecision —— 演示专用巡逻决策器（game_adapter 层，RA-§3.4 权威意图源示例）：
// waypoint 回路巡逻，walk/rest 两阶段交替——walk 期每 tick 持续提出移动意图
// （身体层到达后自动站定），rest 期让位（无候选则站立休息）；
// 黑板 alarm=true 或玩家进入感知范围时同样返回 ready=false 让位给能力候选
// （惊吓/警戒/问候），完整演示"决策器 pending → 模块候选兜底"管线。
// 线程契约：【驱动线程】（Godot 主线程），与 IDecisionMaker 一致。
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::adapter::godot_demo {

class GuardPatrolDecision final : public IDecisionMaker {
public:
    explicit GuardPatrolDecision(std::vector<Vec3> waypoints);

    std::string_view id() const override { return "guard_patrol"; }

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext& tc) override;

    void to_json(json& out) const override;  // 巡逻进度（当前点/阶段计时）
    void from_json(const json& in) override; // 对称恢复；waypoints 由构造注入

private:
    std::optional<Intent> pending() const;              // ready=false 让位意图（RA-§3.4）
    bool player_seen(const core::Blackboard& bb) const; // 感知结果含 player
    bool alarm_on(const core::Blackboard& bb) const;    // 黑板 alarm 置位

    std::vector<Vec3> waypoints_; // 巡逻回路（构造注入，至少 1 点）
    std::size_t current_ = 0;     // 当前巡逻点下标
    bool resting_ = false;        // true=休息阶段（站定让位）
    double phase_left_ = 0.0;     // 当前阶段剩余时间（秒）
};

} // namespace npc_agent::adapter::godot_demo
