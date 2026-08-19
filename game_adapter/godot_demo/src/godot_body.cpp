#include "godot_body.h"

#include <cmath>

namespace npc_agent::adapter::godot_demo {

void GodotBody::bind(godot::Node2D* npc, godot::Sprite2D* sprite, godot::Label* bubble,
                     WorldTransform transform, BodyParams params) {
    npc_ = npc;
    sprite_ = sprite;
    bubble_ = bubble;
    transform_ = transform;
    params_ = params;
}

BodyState GodotBody::body_state() const {
    BodyState state;
    state.position = transform_.to_world(npc_->get_position());
    state.faction = "guard";
    return state;
}

ActionHandle GodotBody::move_to(Vec3 target, float speed) {
    // 单点路径（宿主随后可用 set_path 替换为 A* 航点）。
    path_.assign(1, target);
    path_index_ = 0;
    move_speed_ = speed;
    moving_ = true;
    arrival_pending_ = false;
    last_move_handle_ = ActionHandle{next_handle_++};
    return last_move_handle_;
}

void GodotBody::set_path(std::vector<Vec3> waypoints, float speed) {
    if (waypoints.empty())
        return; // 无路径：维持现状（调用方应传有效航点）
    path_ = std::move(waypoints);
    path_index_ = 0;
    move_speed_ = speed;
    moving_ = true;
    arrival_pending_ = false;
    last_move_handle_ = ActionHandle{next_handle_++};
}

ActionHandle GodotBody::play_emote(const std::string& name) {
    moving_ = false; // 惊吓语义：表情动作打断移动（演示约定）
    show_bubble("❗ " + name, params_.emote_seconds);
    if (name == "startled")
        emote_lock_left_ = params_.emote_seconds; // 惊吓展示期内台词不打断
    return ActionHandle{next_handle_++};
}

ActionHandle GodotBody::say(const DialogueLine& line) {
    moving_ = false;             // 交谈语义：说话时停下（玩家离开感知范围后巡逻恢复）
    if (emote_lock_left_ <= 0.0) // 惊吓展示锁生效时跳过，保证表情可见
        show_bubble(line.text, params_.say_seconds);
    return ActionHandle{next_handle_++};
}

ActionHandle GodotBody::dispatch_game_event(const GameEvent&) {
    return ActionHandle{next_handle_++}; // 示例无领域动作通道需求
}

void GodotBody::update_movement(double dt) {
    if (!moving_ || path_index_ >= path_.size())
        return;
    const godot::Vector2 target_px = transform_.to_pixel(path_[path_index_]);
    const godot::Vector2 pos = npc_->get_position();
    const godot::Vector2 delta_px = target_px - pos;
    const float step = static_cast<float>(move_speed_ * transform_.scale * dt);
    // 朝向：沿移动方向翻转精灵（仅精灵，气泡保持正向可读）。
    if (std::abs(delta_px.x) > 0.001f)
        sprite_->set_scale(godot::Vector2(delta_px.x > 0.0f ? 1.0f : -1.0f, 1.0f));
    if (delta_px.length() <= step + params_.arrive_epsilon * transform_.scale) {
        npc_->set_position(target_px); // 到达当前航点：吸附后进下一段
        ++path_index_;
        if (path_index_ >= path_.size()) {
            moving_ = false;
            arrival_pending_ = true; // 全部航点走完（宿主取走并回投动作完成）
        }
        return;
    }
    npc_->set_position(pos + delta_px.normalized() * step);
}

bool GodotBody::consume_arrival() {
    const bool arrived = arrival_pending_;
    arrival_pending_ = false;
    return arrived;
}

ActionHandle GodotBody::last_move_handle() const {
    return last_move_handle_;
}

bool GodotBody::is_moving() const {
    return moving_;
}

Vec3 GodotBody::path_target() const {
    return path_.empty() ? Vec3{} : path_.back();
}

float GodotBody::move_speed() const {
    return move_speed_;
}

void GodotBody::update_bubble(double dt) {
    if (bubble_time_left_ > 0.0) {
        bubble_time_left_ -= dt;
        if (bubble_time_left_ <= 0.0)
            bubble_->set_visible(false);
    }
    if (emote_lock_left_ > 0.0)
        emote_lock_left_ -= dt;
}

void GodotBody::show_bubble(const std::string& text, double seconds) {
    bubble_->set_text(godot::String::utf8(text.c_str()));
    bubble_->set_visible(true);
    bubble_time_left_ = seconds;
}

} // namespace npc_agent::adapter::godot_demo
