// GodotBody —— Godot 2D 示例的 IAgentBody 实现（game_adapter 层，RA-§3.3）：
// 把框架意图映射为场景动作——move_to 持续移动 NPC 精灵（到达后停下），
// say/play_emote 显示头顶气泡标签（限时隐藏）。动作效果直接体现在场景中，
// 故不再保留动作日志（测试断言用 MockBody，见 npc_agent/testing/）。
// 线程契约：全部方法【驱动线程】（Godot 主线程），与 IAgentBody 一致。
#pragma once

#include <string>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/sprite2d.hpp>

#include "godot_transform.h"
#include "npc_agent/interfaces/i_agent_body.h"

namespace npc_agent::adapter::godot_demo {

class GodotBody final : public IAgentBody {
public:
    // 绑定场景对象与坐标换算（演示装配期调用）。
    void bind(godot::Node2D* npc, godot::Sprite2D* sprite, godot::Label* bubble,
              WorldTransform transform);

    // ---- IAgentBody（RA-§3.3） ----
    BodyState body_state() const override;
    ActionHandle move_to(Vec3 target, float speed) override;       // 更新持续移动目标
    ActionHandle play_emote(const std::string& name) override;     // 气泡显示表情并停下（惊吓语义）
    ActionHandle say(const DialogueLine& line) override;           // 气泡显示台词
    ActionHandle dispatch_game_event(const GameEvent& e) override; // 示例无领域动作，仅回句柄

    // ---- 每帧驱动（由演示节点调用，驱动线程） ----
    void update_movement(double dt); // 朝目标推进并翻转朝向；到达后自动停止
    void update_bubble(double dt);   // 气泡计时到期隐藏

private:
    void show_bubble(const std::string& text, double seconds); // 气泡显示限时

    godot::Node2D* npc_ = nullptr;
    godot::Sprite2D* sprite_ = nullptr; // 朝向翻转目标（仅精灵，气泡不翻转）
    godot::Label* bubble_ = nullptr;
    WorldTransform transform_;
    Vec3 move_target_{};
    float move_speed_ = 0.0f;
    bool moving_ = false;
    double bubble_time_left_ = 0.0;
    double emote_lock_left_ = 0.0; // 惊吓展示锁：期间台词不打断表情（视觉可见性）
    uint64_t next_handle_ = 1;
};

} // namespace npc_agent::adapter::godot_demo
