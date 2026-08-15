#include "npc_agent/testing/mock_world.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace npc_agent::testing {

namespace {

// POD 无运算（RA-§3.1）：数学只在实现内部。
float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length_sq(const Vec3& v) {
    return dot(v, v);
}

} // namespace

void MockWorld::advance(double dt) {
    last_dt_ = dt;
    game_time_ += dt;
    ++tick_index_;
}

void MockWorld::add_entity(std::string id, Vec3 pos, json attributes) {
    entities_.push_back(Entity{std::move(id), pos, std::move(attributes)});
}

void MockWorld::move_entity(std::string_view id, Vec3 pos) {
    for (auto& e : entities_) {
        if (e.id == id)
            e.pos = pos;
    }
}

std::optional<Vec3> MockWorld::entity_pos(std::string_view id) const {
    for (const auto& e : entities_) {
        if (e.id == id)
            return e.pos;
    }
    return std::nullopt;
}

void MockWorld::set_reachable(bool reachable) {
    reachable_ = reachable;
}

const std::vector<Stimulus>& MockWorld::stimuli_log() const {
    return stimuli_log_;
}

TickContext MockWorld::tick_context() const {
    TickContext tc;
    tc.dt = static_cast<float>(last_dt_);
    tc.game_time = game_time_;
    tc.tick_index = tick_index_;
    tc.rng_seed = 0; // 世界不持有随机源（rng_seed 由 Agent 根 RNG 派生）
    return tc;
}

PerceptionResult MockWorld::sense(const PerceptionQuery& q) const {
    PerceptionResult result;
    const float r2 = q.radius * q.radius;
    // 朝向过滤：facing 为零向量视为全向
    const bool directed = length_sq(q.facing) > 0.0f;
    const double half_angle = q.facing_half_angle_deg * 3.14159265358979323846 / 180.0;
    const double min_cos = std::cos(half_angle);

    for (const auto& e : entities_) {
        const Vec3 d{e.pos.x - q.origin.x, e.pos.y - q.origin.y, e.pos.z - q.origin.z};
        if (q.radius > 0.0f && length_sq(d) > r2)
            continue; // 半径过滤
        if (directed) {
            const float facing_len = std::sqrt(length_sq(q.facing));
            const float dir_len = std::sqrt(length_sq(d));
            if (dir_len <= 0.0f) { // 同位置实体视为可见
                result.entities.push_back(PerceivedEntity{e.id, e.pos, e.attributes});
                continue;
            }
            const double cos_angle =
                static_cast<double>(dot(q.facing, d)) /
                (static_cast<double>(facing_len) * static_cast<double>(dir_len));
            if (cos_angle < min_cos)
                continue; // 视野半角过滤
        }
        result.entities.push_back(PerceivedEntity{e.id, e.pos, e.attributes});
    }
    return result;
}

void MockWorld::inject_stimulus(const Stimulus& s) {
    stimuli_log_.push_back(s);
    MemoryEvent e;
    e.subject = s.source_id;
    e.object = "world";
    e.type = "stimulus." + s.type;
    e.timestamp = game_time_;
    e.importance = s.magnitude;
    e.payload = s.payload;
    recent_.push_back(std::move(e));
    if (recent_.size() > 32)
        recent_.pop_front();
}

bool MockWorld::can_reach(Vec3 /*from*/, Vec3 /*to*/) const {
    return reachable_;
}

std::vector<Vec3> MockWorld::find_path(Vec3 from, Vec3 to) const {
    if (!reachable_)
        return {};     // 简化：不可达返回空路径
    return {from, to}; // 简化：直线单段（A* 见阶段 3）
}

WorldSnapshot MockWorld::snapshot() const {
    WorldSnapshot snap;
    snap.game_time = game_time_;
    snap.recent_events.assign(recent_.begin(), recent_.end());
    snap.extra = json{{"entity_count", entities_.size()}};
    return snap;
}

} // namespace npc_agent::testing
