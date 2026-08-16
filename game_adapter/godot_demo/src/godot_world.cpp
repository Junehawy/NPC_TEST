#include "godot_world.h"

#include <cmath>

namespace npc_agent::adapter::godot_demo {

namespace {
constexpr std::size_t kRecentEventCapacity = 32; // 快照近期事件环容量
} // namespace

void GodotWorld::set_transform(WorldTransform transform) {
    transform_ = transform;
}

void GodotWorld::add_entity(std::string id, godot::Node2D* node) {
    entities_.push_back(Entity{std::move(id), node});
}

void GodotWorld::advance(double dt) {
    last_dt_ = dt;
    game_time_ += dt;
    ++tick_index_;
}

TickContext GodotWorld::tick_context() const {
    TickContext tc;
    tc.dt = static_cast<float>(last_dt_);
    tc.game_time = game_time_;
    tc.tick_index = tick_index_;
    tc.rng_seed = 0; // 世界不持有随机源（rng_seed 由 Agent 根 RNG 派生，RA-§3.7）
    return tc;
}

PerceptionResult GodotWorld::sense(const PerceptionQuery& q) const {
    // 演示实现：按欧氏距离过滤。facing 半角过滤默认全向（180°），本示例不实现锥形视野
    // （数据仍随查询传入，宿主实现可自行扩展）。
    PerceptionResult result;
    for (const auto& entity : entities_) {
        const Vec3 pos = transform_.to_world(entity.node->get_position());
        const float dx = pos.x - q.origin.x;
        const float dy = pos.y - q.origin.y;
        if (std::sqrt(dx * dx + dy * dy) > q.radius)
            continue;
        result.entities.push_back(PerceivedEntity{entity.id, pos});
    }
    return result;
}

void GodotWorld::inject_stimulus(const Stimulus& s) {
    stimuli_log_.push_back(s);
    MemoryEvent e;
    e.subject = s.source_id;
    e.object = s.type;
    e.type = "stimulus";
    e.timestamp = game_time_;
    e.importance = s.magnitude;
    recent_.push_back(std::move(e));
    while (recent_.size() > kRecentEventCapacity)
        recent_.pop_front();
}

bool GodotWorld::can_reach(Vec3, Vec3) const {
    return true;
}

std::vector<Vec3> GodotWorld::find_path(Vec3, Vec3 to) const {
    return {to}; // 示例场景无障碍物，路径即直达
}

WorldSnapshot GodotWorld::snapshot() const {
    WorldSnapshot snap;
    snap.game_time = game_time_;
    snap.recent_events.assign(recent_.begin(), recent_.end());
    json entities = json::array();
    for (const auto& entity : entities_) {
        const Vec3 pos = transform_.to_world(entity.node->get_position());
        entities.push_back(json{{"id", entity.id}, {"pos", {pos.x, pos.y, pos.z}}});
    }
    snap.extra = json{{"entities", std::move(entities)}};
    return snap;
}

std::optional<Vec3> GodotWorld::entity_pos(std::string_view id) const {
    for (const auto& entity : entities_) {
        if (entity.id == id)
            return transform_.to_world(entity.node->get_position());
    }
    return std::nullopt;
}

const std::vector<Stimulus>& GodotWorld::stimuli_log() const {
    return stimuli_log_;
}

} // namespace npc_agent::adapter::godot_demo
