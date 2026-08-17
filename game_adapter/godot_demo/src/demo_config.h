// DemoConfig —— Godot 演示专用参数集（game_adapter 层，宿主自解释扩展，RA-§3.2 extra 通道）：
// 全部演示行为参数集中定义于 NPC 配置 JSON 的 extra.demo 段（框架仅透传 extra，
// 见 AgentConfig::extra，本模块不做框架解析），解析为 fail-fast：
// 类型错误 / 未知键 / 非法值（非正时长、空巡逻点、越界窗口等）即启动报错。
// 各字段默认值 = 演示原始行为，缺省字段保持向后兼容。
// 线程契约：【驱动线程】（装配期一次性解析）。
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "npc_agent/interfaces/types.h"

namespace npc_agent::adapter::godot_demo {

// 巡逻决策器参数（GuardPatrolDecision）：waypoint 回路 + walk/rest 时间片。
struct PatrolParams {
    std::vector<Vec3> waypoints{
        {-3.0f, 0.0f, 0.0f}, {0.0f, -1.5f, 0.0f}, {3.0f, 0.0f, 0.0f}, {0.0f, 1.5f, 0.0f}};
    double walk_seconds = 2.5;        // walk 阶段时长（身体层到达后提前站定）
    double rest_seconds = 1.0;        // rest 阶段时长（站定休息）
    float speed = 2.0f;               // 巡逻移速（世界单位/秒）
    bool yield_on_player_seen = true; // 玩家可见时让位仲裁（问候）
};

// 问候能力参数（GreetCapability）。
struct GreetParams {
    bool enabled = true;
    std::string text = "你好，旅行者";
    std::string tone = "friendly";
    float priority = 2.0f;
    float max_distance = 2.0f; // 触发距离上限（需 ≤ perception.radius，装配期交叉校验）
};

// 惊吓能力参数（StartleCapability）。
struct StartleParams {
    std::string stimulus_type = "gunshot"; // 触发的刺激类型
    std::string emote = "startled";
    float priority = 5.0f;
};

// 警戒能力参数（AlertCapability）。
struct AlertParams {
    std::string emote = "警戒";
    float priority = 1.5f;
};

// 身体动作参数（GodotBody）。
struct BodyParams {
    double say_seconds = 2.5;     // 台词气泡时长
    double emote_seconds = 1.2;   // 表情气泡时长（惊吓展示锁同值）
    double arrive_epsilon = 0.05; // 到达判定（世界单位）
};

// 玩家参数（player.gd 运行期读取）。
struct PlayerParams {
    float speed = 400.0f;       // 移动速度（像素/秒）
    float clamp_margin = 24.0f; // 窗口边界钳制边距（像素）
};

// 场景参数（演示节点装配期读取）。
struct SceneParams {
    Vec3 npc_spawn{};                    // NPC 出生点（世界坐标）
    Vec3 player_spawn{3.5f, 0.0f, 0.0f}; // 玩家出生点（世界坐标）
    float scale = 100.0f;                // 像素 / 世界单位
    int window_width = 960;              // 窗口尺寸（像素）
    int window_height = 540;
    bool show_debug = true; // 调试面板开关
    bool show_hint = true;  // 操作提示开关
};

// 阶段 2 接入参数（R7-10）：fsm.enabled=true 时演示装配改用框架 FsmDecisionMaker +
// PerceptionModule（替代旧的巡逻决策器与玩具能力），行为链由 definition 数据驱动。
struct FsmDemoParams {
    bool enabled = false;
    nlohmann::json definition = nlohmann::json::object(); // FsmDecisionMaker 定义（透传校验）
    double stimulus_window_seconds = 3.0;                 // 感知时间窗（heard_<type> 旗标保持时长）
    float player_near_distance = 2.0f;                    // 玩家近距阈值（写 player_near 旗标）
};

// 演示参数全集（默认值 = 演示原始行为）。
struct DemoConfig {
    PatrolParams patrol;
    double alarm_seconds = 3.0; // 枪声后警戒时长（决策器 pending 窗口）
    GreetParams greet;
    StartleParams startle;
    AlertParams alert;
    BodyParams body;
    PlayerParams player;
    SceneParams scene;
    FsmDemoParams fsm; // 阶段 2 模式（可选；enabled=true 时启用）
};

// 从 AgentConfig::extra 解析 extra.demo 段；extra 为空对象时使用全默认值。
// 成功返回 nullopt；失败返回错误信息（含字段路径，fail-fast）。
std::optional<std::string> parse_demo_config(const nlohmann::json& extra, DemoConfig& out);

} // namespace npc_agent::adapter::godot_demo
