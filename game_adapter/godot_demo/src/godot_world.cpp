#include "godot_world.h"

#include <algorithm>
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

void GodotWorld::set_grid_nav(testing::GridNav* grid) {
    grid_ = grid;
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

bool GodotWorld::can_reach(Vec3 from, Vec3 to) const {
    if (grid_ == nullptr)
        return true; // 无导航网格：直通可达（旧行为）
    return grid_->can_reach(from, to);
}

std::vector<Vec3> GodotWorld::find_path(Vec3 from, Vec3 to) const {
    if (grid_ == nullptr)
        return {to}; // 无导航网格：路径即直达（旧行为）
    std::vector<Vec3> path = grid_->find_path(from, to);
    if (!path.empty())
        return path;
    // 终点被阻塞/不可达（如木箱正好压在目标点）：在终点周围找最近可达单元
    // 作为替代目的地（fallback），保证 NPC 总有合理终点可去（R10-9）。
    // 仅当目标 cell 确实被阻塞时触发：若目标可达但 start==goal（已就位），
    // find_path 返回空是"无需移动"语义，fallback 会把终点改到旁边 cell，
    // 导致路径每帧抖动、move_done 永不触发（R10-9 回归）。
    const auto goal = grid_->world_to_cell(to);
    if (!goal.has_value() || !grid_->is_blocked(goal->first, goal->second))
        return path;
    for (int r = 1; r <= 10; ++r) { // 半径 10 单元（2.5u）内螺旋搜索
        for (int dc = -r; dc <= r; ++dc) {
            for (int dr = -r; dr <= r; ++dr) {
                if (std::max(std::abs(dc), std::abs(dr)) != r)
                    continue; // 只在当前环上搜索
                const int c = goal->first + dc;
                const int row = goal->second + dr;
                if (!grid_->in_bounds(c, row) || grid_->is_blocked(c, row))
                    continue;
                const auto alt = grid_->find_path(from, grid_->cell_to_world(c, row));
                if (!alt.empty())
                    return alt;
            }
        }
    }
    return path; // 周围也全堵：保持空路径（宿主自行等待）
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
