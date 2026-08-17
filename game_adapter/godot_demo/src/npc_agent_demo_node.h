// NpcAgentDemoNode —— 演示根节点（GDExtension 注册到 ClassDB 的场景类，RA-§8.2）：
// _ready 装配框架（配置加载 → 参数解析 → 适配器 → AgentSystem → 决策器/能力），
// _process 按帧驱动 advance → 移动推进 → tick → 调试面板刷新。
// 全部演示行为参数来自 NPC 配置 JSON 的 extra.demo 段（DemoConfig，fail-fast）；
// 配置路径可用命令行用户参数覆盖：godot ... -- --config res://路径.json。
// 玩家输入由 GDScript（scripts/player.gd）处理，并通过 inject_gunshot()
// 跨语言调用回本节点，演示 GDScript↔C++ 扩展边界。
// 线程契约：全部方法【驱动线程】（Godot 主线程）。
#pragma once

#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node2d.hpp>

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

    // 玩家开火（GDScript 调用）：注入枪声刺激，下一 tick NPC 出惊吓表情；
    // 同时置黑板 alarm（决策器进入 pending），警戒 alarm_seconds 后恢复巡逻——
    // 期间能力候选参与仲裁（问候/惊吓/警戒），演示完整仲裁管线（RA-§3.4）。
    void inject_gunshot();

protected:
    static void _bind_methods();

private:
    std::string resolve_config_path() const; // 命令行 --config 覆盖 / 默认路径
    bool parse_config();                     // 读配置并解析框架+演示参数；失败返回 false
    void setup_agent();                      // 装配 AgentSystem（决策器/能力，参数来自配置）
    void build_scene();                      // 场景树装配（窗口/精灵/气泡/玩家/面板）
    void update_debug_label();               // 调试面板刷新
    void inject_player_distance();           // 黑板注入 player_distance（问候距离判定用）
    void log_status(const std::string& msg); // 启动状态输出（控制台）

    DemoConfig cfg_;
    core::AgentConfig agent_cfg_;
    core::AgentSystem system_;
    GodotWorld world_;
    GodotBody body_;
    core::Agent* agent_ = nullptr;
    bool ready_ = false;

    // 场景对象（场景树持有，本类不负责析构）。
    godot::Node2D* npc_node_ = nullptr;
    godot::Node2D* player_node_ = nullptr;
    godot::Label* bubble_label_ = nullptr;
    godot::Label* debug_label_ = nullptr;
    double alarm_time_left_ = 0.0; // 警戒剩余时间（>0 时黑板 alarm=true）
};

} // namespace npc_agent::adapter::godot_demo
