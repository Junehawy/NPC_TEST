#include "godot_body.h"

#include <cmath>

namespace npc_agent::adapter::godot_demo {

namespace {
constexpr double kArriveEpsilon = 0.05; // 到达判定（世界单位）
constexpr double kSaySeconds = 2.5;     // 台词气泡时长
constexpr double kEmoteSeconds = 1.2;   // 表情气泡时长
} // namespace

void GodotBody::bind(godot::Node2D* npc, godot::Label* bubble, WorldTransform transform) {
    npc_ = npc;
    bubble_ = bubble;
    transform_ = transform;
}

BodyState GodotBody::body_state() const {
    BodyState state;
    state.position = transform_.to_world(npc_->get_position());
    state.faction = "guard";
    return state;
}

ActionHandle GodotBody::move_to(Vec3 target, float speed) {
    move_target_ = target;
    move_speed_ = speed;
    moving_ = true;
    return ActionHandle{next_handle_++};
}

ActionHandle GodotBody::play_emote(const std::string& name) {
    moving_ = false; // 惊吓语义：表情动作打断移动（演示约定）
    show_bubble("❗ " + name, kEmoteSeconds);
    return ActionHandle{next_handle_++};
}

ActionHandle GodotBody::say(const DialogueLine& line) {
    show_bubble(line.text, kSaySeconds);
    return ActionHandle{next_handle_++};
}

ActionHandle GodotBody::dispatch_game_event(const GameEvent&) {
    return ActionHandle{next_handle_++}; // 示例无领域动作通道需求
}

void GodotBody::update_movement(double dt) {
    if (!moving_)
        return;
    const godot::Vector2 target_px = transform_.to_pixel(move_target_);
    const godot::Vector2 pos = npc_->get_position();
    const godot::Vector2 delta_px = target_px - pos;
    const float step = static_cast<float>(move_speed_ * transform_.scale * dt);
    if (delta_px.length() <= step + kArriveEpsilon * transform_.scale) {
        npc_->set_position(target_px); // 最后一步直接吸附，避免抖动
        moving_ = false;
        return;
    }
    npc_->set_position(pos + delta_px.normalized() * step);
}

void GodotBody::update_bubble(double dt) {
    if (bubble_time_left_ <= 0.0)
        return;
    bubble_time_left_ -= dt;
    if (bubble_time_left_ <= 0.0)
        bubble_->set_visible(false);
}

void GodotBody::show_bubble(const std::string& text, double seconds) {
    bubble_->set_text(godot::String::utf8(text.c_str()));
    bubble_->set_visible(true);
    bubble_time_left_ = seconds;
}

} // namespace npc_agent::adapter::godot_demo
