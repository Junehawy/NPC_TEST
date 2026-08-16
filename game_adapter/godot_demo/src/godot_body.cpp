#include "godot_body.h"

#include <cmath>

namespace npc_agent::adapter::godot_demo {

namespace {
constexpr double kArriveEpsilon = 0.05; // 到达判定（世界单位）
constexpr double kSaySeconds = 2.5;     // 台词气泡时长
constexpr double kEmoteSeconds = 1.2;   // 表情气泡时长
} // namespace

void GodotBody::bind(godot::Node2D* npc, godot::Sprite2D* sprite, godot::Label* bubble,
                     WorldTransform transform) {
    npc_ = npc;
    sprite_ = sprite;
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
    if (name == "startled")
        emote_lock_left_ = kEmoteSeconds; // 惊吓展示期内台词不打断
    return ActionHandle{next_handle_++};
}

ActionHandle GodotBody::say(const DialogueLine& line) {
    moving_ = false;             // 交谈语义：说话时停下（玩家离开感知范围后巡逻恢复）
    if (emote_lock_left_ <= 0.0) // 惊吓展示锁生效时跳过，保证表情可见
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
    // 朝向：沿移动方向翻转精灵（仅精灵，气泡保持正向可读）。
    if (std::abs(delta_px.x) > 0.001f)
        sprite_->set_scale(godot::Vector2(delta_px.x > 0.0f ? 1.0f : -1.0f, 1.0f));
    npc_->set_position(pos + delta_px.normalized() * step);
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
