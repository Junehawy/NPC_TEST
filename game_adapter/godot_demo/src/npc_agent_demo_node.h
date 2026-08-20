// NpcAgentDemoNode —— 演示根节点（GDExtension 注册到 ClassDB 的场景类，RA-§8.2）：
// _ready 装配框架（配置加载 → 参数解析 → 适配器 → AgentSystem → 决策器/能力），
// _process 按帧驱动 advance → 移动推进 → tick → 调试面板刷新。
// 两种装配模式（由配置决定）：
//  - 单 NPC 模式（scene.npcs 为空）：旧巡逻决策器+玩具能力 或 框架 FSM（fsm.enabled）；
//  - 多 NPC 模式（scene.npcs 非空，R7-11）：每个 NPC 独立 Agent + FSM + 感知模块，
//    宿主注入 player_distance/player_near 旗标；"呼叫支援"台词经宿主声学传播
//    （shout_when_say）转为 stimulus.shout，驱动其他 NPC 响应（连锁反应）。
// 玩家输入由 GDScript（scripts/player.gd）处理，并通过 inject_gunshot()
// 跨语言调用回本节点。线程契约：全部方法【驱动线程】（Godot 主线程）。
#pragma once

#include <string>
#include <vector>

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/sprite2d.hpp>

#include "demo_config.h"
#include "godot_body.h"
#include "godot_world.h"
#include "npc_agent/core/agent_system.h"

namespace npc_agent::adapter::godot_demo {

class NpcAgentDemoNode : public godot::Node {
    GDCLASS(NpcAgentDemoNode, godot::Node)

public:
    void _ready() override;
    void _process(double delta) override;

    // 玩家开火（GDScript 调用）：注入枪声刺激，经感知模块驱动各 NPC 反应。
    void inject_gunshot();

    // 玩家放置障碍（GDScript 调用，E 键）：沿最近移动方向在前方放置 2×2 木箱
    // （避开脚下与死角），阻塞网格 + 视觉方块 + 移动中 NPC 重规划。
    void place_obstacle(float dir_x, float dir_y);

    // 玩家碰撞查询（GDScript 调用）：像素位置是否落在阻塞单元（player.gd 移动前检查）。
    bool is_pixel_blocked(float px, float py);

protected:
    static void _bind_methods();

private:
    // 单个 NPC 运行实例（多 NPC 模式）。
    struct NpcInstance {
        core::Agent* agent = nullptr;
        godot::Node2D* node = nullptr;
        godot::Sprite2D* sprite = nullptr;
        godot::Label* bubble = nullptr;
        GodotBody body;
        bool shout_sent = false; // 呼叫支援台词已转发为 stimulus.shout（边沿触发）
        bool shout_when_say = false;
        float player_near_distance = 2.0f;         // 近距阈值（规格注入）
        std::string label;                         // 显示名（中文名牌/面板，规格注入）
        std::vector<godot::ColorRect*> path_dots;  // 路线可视化航点标记（阶段 3，R10）
        godot::ColorRect* target_marker = nullptr; // 当前移动目标标记
    };

    std::string resolve_config_path() const; // 命令行 --config 覆盖 / 默认路径
    bool parse_config();                     // 读配置并解析框架+演示参数；失败返回 false
    bool setup_agents();                     // 装配 AgentSystem（决策器/能力，参数来自配置）
    bool setup_single_npc();                 // 单 NPC 模式装配（旧巡逻 或 框架 FSM）
    bool setup_multi_npc();                  // 多 NPC 模式装配（每 NPC 独立 FSM+感知）
    void build_scene();                      // 场景树装配（窗口/精灵/气泡/玩家/面板）
    void build_single_npc_scene(const WorldTransform& transform); // 单 NPC 场景
    void build_multi_npc_scene(const WorldTransform& transform);  // 多 NPC 场景（规格表）
    void draw_map(int width, int height);               // 装饰地图（地面/道路/建筑/树木，R7-12）
    void register_map_obstacles(int width, int height); // 建筑→网格障碍（阶段 3）
    void plan_paths_and_report(); // 移动意图→A* 路径注入 + 到达回投（阶段 3，R10）
    void update_path_visual(NpcInstance& npc, const std::vector<Vec3>& path); // 路线标记刷新
    void update_debug_label(); // 调试面板刷新（多 NPC 逐行）
    void inject_player_flags(core::Agent& agent, float near_distance); // 距离/近距旗标
    void propagate_shouts();                 // 呼叫支援台词 → stimulus.shout（声学传播）
    void log_status(const std::string& msg); // 启动状态输出（控制台）

    DemoConfig cfg_;
    core::AgentConfig agent_cfg_;
    core::AgentSystem system_;
    testing::GridNav grid_{38, 21, 0.25f}; // 宿主导航网格（build_scene 按窗口重算）
    GodotWorld world_;
    GodotBody body_;                // 单 NPC 模式的身体
    core::Agent* agent_ = nullptr;  // 单 NPC 模式的 Agent
    std::vector<NpcInstance> npcs_; // 多 NPC 模式实例
    bool ready_ = false;

    // 场景对象（场景树持有，本类不负责析构）。
    godot::Node2D* npc_node_ = nullptr; // 单 NPC 模式
    godot::Node2D* player_node_ = nullptr;
    godot::Label* bubble_label_ = nullptr;
    godot::Label* debug_label_ = nullptr;
    double alarm_time_left_ = 0.0; // 警戒剩余时间（旧模式；>0 时黑板 alarm=true）
};

} // namespace npc_agent::adapter::godot_demo
