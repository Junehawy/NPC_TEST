#include "guard_patrol_decision.h"

namespace npc_agent::adapter::godot_demo {

std::optional<Intent> GuardPatrolDecision::propose(const core::Blackboard& bb,
                                                   const TickContext& tc) {
    // 1) 警戒或（配置允许且）玩家在场：让位能力候选，展示完整仲裁管线（RA-§3.4）。
    if (alarm_on(bb) || (params_.yield_on_player_seen && player_seen(bb)))
        return pending();

    // 2) 回路巡逻：walk 期持续提出移动意图；rest 期站定让位。
    if (params_.waypoints.empty())
        return pending();
    phase_left_ -= static_cast<double>(tc.dt);
    if (!resting_) {
        if (phase_left_ > 0.0) {
            Intent intent;
            intent.payload = MoveIntent{params_.waypoints[current_], params_.speed};
            intent.priority = 1.0f;
            return intent;
        }
        resting_ = true;
        phase_left_ = params_.rest_seconds;
        return pending();
    }
    if (phase_left_ > 0.0)
        return pending();
    resting_ = false;
    phase_left_ = params_.walk_seconds;
    current_ = (current_ + 1) % params_.waypoints.size();
    Intent intent;
    intent.payload = MoveIntent{params_.waypoints[current_], params_.speed};
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
