// GodotBody —— Godot 2D 示例的 IAgentBody 实现（game_adapter 层，RA-§3.3）：
// 把框架意图映射为场景动作——move_to 持续移动 NPC 精灵（到达后停下），
// say/play_emote 显示头顶气泡标签（限时隐藏）。动作效果直接体现在场景中，
// 故不再保留动作日志（测试断言用 MockBody，见 npc_agent/testing/）。
// 气泡时长/到达阈值等行为参数来自 DemoConfig（BodyParams）。
// 线程契约：全部方法【驱动线程】（Godot 主线程），与 IAgentBody 一致。
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/sprite2d.hpp>

#include "demo_config.h"
#include "godot_transform.h"
#include "npc_agent/interfaces/i_agent_body.h"

namespace npc_agent::adapter::godot_demo {

class GodotBody final : public IAgentBody {
public:
    // 绑定场景对象、坐标换算与行为参数（演示装配期调用）。
    void bind(godot::Node2D* npc, godot::Sprite2D* sprite, godot::Label* bubble,
              WorldTransform transform, BodyParams params);

    // ---- IAgentBody（RA-§3.3） ----
    BodyState body_state() const override;
    ActionHandle move_to(Vec3 target, float speed) override;       // 更新持续移动目标
    ActionHandle play_emote(const std::string& name) override;     // 气泡显示表情并停下（惊吓语义）
    ActionHandle say(const DialogueLine& line) override;           // 气泡显示台词
    ActionHandle dispatch_game_event(const GameEvent& e) override; // 示例无领域动作，仅回句柄

    // ---- 每帧驱动（由演示节点调用，驱动线程） ----
    // 宿主路径注入（阶段 3，R10）：把 A* 航点喂给身体，move_to 后由宿主调用；
    // 身体按航点序列行进，全部到达后置 arrival 待取。
    void set_path(std::vector<Vec3> waypoints, float speed);
    void update_movement(double dt); // 沿路径推进并翻转朝向；到达后自动停止
    void update_bubble(double dt);   // 气泡计时到期隐藏

    // 到达观测（阶段 3）：上次移动全部航点走完后返回 true 并复位（每段一次）。
    // 宿主据此 report_action_result(handle, "completed") → move_done 驱动 FSM。
    bool consume_arrival();
    ActionHandle last_move_handle() const; // 最近一次 move_to/set_path 的句柄

    // 路径观测（宿主重规划用，阶段 3）。
    bool is_moving() const;
    const std::vector<Vec3>& path() const; // 当前航点序列
    Vec3 path_target() const;              // 当前路径终点（无路径返回零向量）
    float move_speed() const;

    // 宿主碰撞检查（阶段 3，R10）：非空时移动不得进入阻塞位置（停止而非穿行）。
    void set_blocked_check(std::function<bool(Vec3 world)> check);

private:
    void show_bubble(const std::string& text, double seconds); // 气泡显示限时

    godot::Node2D* npc_ = nullptr;
    godot::Sprite2D* sprite_ = nullptr; // 朝向翻转目标（仅精灵，气泡不翻转）
    godot::Label* bubble_ = nullptr;
    WorldTransform transform_;
    BodyParams params_;
    std::function<bool(Vec3)> blocked_check_; // 宿主碰撞检查（空 = 不检查）
    std::vector<Vec3> path_{};                // 当前路径航点（世界坐标；move_to 时为单点）
    std::size_t path_index_ = 0;
    float move_speed_ = 0.0f;
    bool moving_ = false;
    bool arrival_pending_ = false; // 到达待取（宿主 consume_arrival 读取）
    ActionHandle last_move_handle_{};
    double bubble_time_left_ = 0.0;
    double emote_lock_left_ = 0.0; // 惊吓展示锁：期间台词不打断表情（视觉可见性）
    uint64_t next_handle_ = 1;
};

} // namespace npc_agent::adapter::godot_demo
