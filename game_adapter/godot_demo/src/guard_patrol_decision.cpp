#include "guard_patrol_decision.h"

#include <utility>

namespace npc_agent::adapter::godot_demo {

namespace {
constexpr double kWalkSeconds = 2.5; // walk 阶段时长（身体层到达后提前站定）
constexpr double kRestSeconds = 1.0; // rest 阶段时长（站定休息）
constexpr float kPatrolSpeed = 2.0f; // 巡逻移速（世界单位/秒）
} // namespace

GuardPatrolDecision::GuardPatrolDecision(std::vector<Vec3> waypoints)
    : waypoints_(std::move(waypoints)), phase_left_(kWalkSeconds) {
    current_ = 1; // 首个巡逻目标为回路第二点（先向上巡，远离玩家出生侧）
}

std::optional<Intent> GuardPatrolDecision::propose(const core::Blackboard& bb,
                                                   const TickContext& tc) {
    // 1) 警戒或玩家在场：让位能力候选（惊吓/警戒/问候），展示完整仲裁管线（RA-§3.4）。
    if (alarm_on(bb) || player_seen(bb))
        return pending();

    // 2) 回路巡逻：walk 期持续提出移动意图；rest 期站定让位。
    if (waypoints_.empty())
        return pending();
    phase_left_ -= static_cast<double>(tc.dt);
    if (!resting_) {
        if (phase_left_ > 0.0) {
            Intent intent;
            intent.payload = MoveIntent{waypoints_[current_], kPatrolSpeed};
            intent.priority = 1.0f;
            return intent;
        }
        resting_ = true;
        phase_left_ = kRestSeconds;
        return pending();
    }
    if (phase_left_ > 0.0)
        return pending();
    resting_ = false;
    phase_left_ = kWalkSeconds;
    current_ = (current_ + 1) % waypoints_.size();
    Intent intent;
    intent.payload = MoveIntent{waypoints_[current_], kPatrolSpeed};
    intent.priority = 1.0f;
    return intent;
}

void GuardPatrolDecision::to_json(json& out) const {
    out = json{{"current", current_}, {"resting", resting_}, {"phase_left", phase_left_}};
}

void GuardPatrolDecision::from_json(const json& in) {
    if (!in.is_object())
        return;
    current_ = in.value("current", std::size_t{0});
    resting_ = in.value("resting", false);
    phase_left_ = in.value("phase_left", 0.0);
}

std::optional<Intent> GuardPatrolDecision::pending() const {
    Intent intent;
    intent.payload = MoveIntent{};
    intent.priority = 1.0f;
    intent.ready = false;
    return intent;
}

bool GuardPatrolDecision::player_seen(const core::Blackboard& bb) const {
    const auto* seen = bb.get("perceived_entities");
    if (seen == nullptr || !seen->is_array())
        return false;
    for (const auto& id : *seen) {
        if (id.is_string() && id.get<std::string>() == "player")
            return true;
    }
    return false;
}

bool GuardPatrolDecision::alarm_on(const core::Blackboard& bb) const {
    const auto* alarm = bb.get("alarm");
    return alarm != nullptr && alarm->is_boolean() && alarm->get<bool>();
}

} // namespace npc_agent::adapter::godot_demo
