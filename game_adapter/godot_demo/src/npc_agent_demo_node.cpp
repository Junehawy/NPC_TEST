#include "npc_agent_demo_node.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/polygon2d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "alert_capability.h"
#include "greet_capability.h"
#include "guard_patrol_decision.h"
#include "npc_agent/capabilities/perception_module.h"
#include "npc_agent/core/agent_config.h"
#include "npc_agent/decision/fsm_decision_maker.h"
#include "npc_agent/interfaces/intent.h"
#include "npc_agent/testing/grid_nav.h"
#include "npc_agent/testing/intent_desc.h"
#include "npc_agent/testing/move_done_capability.h"
#include "startle_capability.h"

namespace npc_agent::adapter::godot_demo {

namespace {
constexpr const char* kDefaultConfigPath = "res://assets/npcs/sample_guard.json";
constexpr const char* kPlayerScriptPath = "res://scripts/player.gd";
constexpr const char* kNpcSpritePath = "res://assets/sprites/npc.svg";
constexpr const char* kPlayerSpritePath = "res://assets/sprites/player.svg";

constexpr const char* kHintText = "WASD 移动 · 空格 枪声刺激 · 靠近 NPC 触发问候 · 枪声后进入警戒";
constexpr const char* kHintTextFsm =
    "阶段 2 FSM：WASD 移动 · 空格 枪声 → 警戒 → 呼叫支援 → 搜寻 · 靠近触发问候";
constexpr const char* kHintTextMulti =
    "阶段 3 多NPC：WASD 移动（撞墙停）· 空格 枪声 · E 放木箱 → NPC 绕行（蓝点=路线）· 靠近问候";

constexpr const char* kShoutMarker = "呼叫支援"; // 台词标记（宿主声学传播触发条件）

// 地图建筑（世界坐标矩形 x,y,w,h）：两栋挡在 y=0 主路线（守卫巡逻/搜寻必经），
// 一栋挡在支援兵响应路线，其余为装饰——寻路绕行因此可见（R10）。
struct MapBuilding {
    float x;
    float y;
    float w;
    float h;
    godot::Color color;
};
const MapBuilding kBuildings[] = {
    {-1.0f, -0.6f, 0.8f, 1.2f, godot::Color(0.55f, 0.35f, 0.25f, 1.0f)}, // 挡守卫巡逻
    {0.2f, -0.6f, 0.8f, 1.2f, godot::Color(0.45f, 0.45f, 0.52f, 1.0f)},  // 挡守卫搜寻
    {-2.8f, 1.2f, 1.2f, 0.9f, godot::Color(0.62f, 0.52f, 0.30f, 1.0f)},  // 市集棚
    {1.2f, 0.7f, 1.2f, 1.0f, godot::Color(0.50f, 0.32f, 0.28f, 1.0f)},   // 挡支援兵响应
    {-4.6f, -2.4f, 1.5f, 1.0f, godot::Color(0.36f, 0.36f, 0.40f, 1.0f)}, // 石屋
    {-4.6f, 2.2f, 1.2f, 0.8f, godot::Color(0.40f, 0.30f, 0.22f, 1.0f)},  // 木屋
    {2.6f, -2.2f, 1.2f, 0.8f, godot::Color(0.45f, 0.42f, 0.36f, 1.0f)},  // 仓库
};

// 池塘（障碍，蓝色）与围墙（障碍，灰色细带）——丰富地图层次（R10）。
const MapBuilding kPond[] = {
    {-3.6f, -1.8f, 1.0f, 0.7f, godot::Color(0.24f, 0.45f, 0.75f, 1.0f)},
};
const MapBuilding kFence[] = {
    {-4.8f, 3.2f, 9.6f, 0.16f, godot::Color(0.52f, 0.48f, 0.42f, 1.0f)},
};

// 树木（世界坐标树心；树冠半径 26px ≈ 0.26u → 注册为单单元障碍，不可穿过）。
const Vec3 kTreePos[] = {{-4.2f, -1.8f, 0.0f}, {-3.4f, 2.6f, 0.0f},  {4.4f, -2.2f, 0.0f},
                         {5.6f, 2.4f, 0.0f},   {-1.6f, -2.2f, 0.0f}, {-0.4f, 2.9f, 0.0f},
                         {3.8f, 2.8f, 0.0f},   {5.8f, -2.6f, 0.0f},  {-2.2f, -2.8f, 0.0f}};

// 智能体（NPC/玩家）动态占据半径（世界单位）：互不穿透的最近间距。
constexpr float kAgentRadius = 0.35f;
} // namespace

void NpcAgentDemoNode::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("inject_gunshot"),
                                &NpcAgentDemoNode::inject_gunshot);
    godot::ClassDB::bind_method(godot::D_METHOD("place_obstacle", "dir_x", "dir_y"),
                                &NpcAgentDemoNode::place_obstacle);
    godot::ClassDB::bind_method(godot::D_METHOD("is_pixel_blocked"),
                                &NpcAgentDemoNode::is_pixel_blocked);
}

void NpcAgentDemoNode::_ready() {
    add_to_group("npc_demo"); // GDScript 侧经 group 定位本节点（跨场景嵌套可用）
    if (!parse_config()) {
        ready_ = false;
        return;
    }
    build_scene(); // 先建场景（身体绑定），再装配 Agent（顺序依赖：body 绑定先于挂接）
    if (!setup_agents()) {
        ready_ = false;
        return;
    }
    ready_ = true;
    if (npcs_.empty())
        log_status("NPC 智能体已就绪：" + std::string(agent_->id()));
    else
        log_status("多 NPC 智能体已就绪：" + std::to_string(npcs_.size()) + " 个");
}

void NpcAgentDemoNode::_process(double delta) {
    if (!ready_)
        return;
    world_.advance(delta); // 世界时间先于 tick 推进（与无头示例一致）
    // 警戒计时（旧单 NPC 巡逻模式）：到期解除黑板 alarm（RA-§3.4 pending 兜底）。
    if (npcs_.empty() && !cfg_.fsm.enabled && alarm_time_left_ > 0.0) {
        alarm_time_left_ -= delta;
        if (alarm_time_left_ <= 0.0)
            agent_->blackboard().set("alarm", false);
    }
    if (npcs_.empty()) {
        body_.update_movement(delta);
        body_.update_bubble(delta);
    } else {
        for (auto& npc : npcs_) {
            npc.body.update_movement(delta);
            npc.body.update_bubble(delta);
        }
    }
    // 宿主注入派生状态（距离/近距旗标，黑板契约内；多 NPC 各自独立黑板）。
    if (npcs_.empty()) {
        inject_player_flags(*agent_, cfg_.fsm.enabled ? cfg_.fsm.player_near_distance : -1.0f);
    } else {
        for (auto& npc : npcs_)
            inject_player_flags(*npc.agent, npc.player_near_distance);
    }
    // R10 到达回投：先于 tick 消费身体到达标志并回报动作完成。若放在 tick 之后，
    // FSM 每 tick 重发同一移动意图会经 execute→move_to 重置到达标志（丢失 move_done）。
    if (!npcs_.empty()) {
        for (auto& npc : npcs_) {
            if (npc.body.consume_arrival())
                npc.agent->report_action_result(npc.body.last_move_handle(), "completed");
        }
    }
    system_.tick();
    if (!npcs_.empty()) {
        propagate_shouts();      // 呼叫支援台词 → stimulus.shout（连锁反应）
        plan_paths_and_report(); // 移动意图 → A* 路径注入（阶段 3）
    }
    update_debug_label(); // 演示规模：每帧刷新面板，保证瞬时意图可见
    // 玩家反馈气泡计时（E 放置结果提示）。
    if (player_bubble_ != nullptr && player_bubble_left_ > 0.0) {
        player_bubble_left_ -= delta;
        if (player_bubble_left_ <= 0.0)
            player_bubble_->set_visible(false);
    }
}

void NpcAgentDemoNode::inject_gunshot() {
    if (!ready_)
        return;
    const auto source_pos = world_.entity_pos("player").value_or(Vec3{});
    system_.inject_stimulus(Stimulus{cfg_.startle.stimulus_type, source_pos, 1.0f, "player"});
    if (!npcs_.empty() || cfg_.fsm.enabled)
        return; // FSM 模式：heard_gunshot 旗标由框架感知模块置位，无需黑板 alarm
    // 旧单 NPC 巡逻模式：黑板置位使巡逻决策器返回 pending（RA-§3.4）。
    agent_->blackboard().set("alarm", true);
    alarm_time_left_ = cfg_.alarm_seconds;
}

std::string NpcAgentDemoNode::resolve_config_path() const {
    // 配置路径解析优先级：环境变量 NPC_DEMO_CONFIG → 命令行用户参数 --config
    // （-- 之后）→ 默认路径。环境变量通道对 Movie Maker 等不转发用户参数的模式友好。
    const godot::String env_cfg = godot::OS::get_singleton()->get_environment("NPC_DEMO_CONFIG");
    if (!env_cfg.is_empty())
        return env_cfg.utf8().get_data();
    const godot::PackedStringArray args = godot::OS::get_singleton()->get_cmdline_user_args();
    for (int64_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--config")
            return args[i + 1].utf8().get_data();
    }
    return kDefaultConfigPath;
}

bool NpcAgentDemoNode::parse_config() {
    // 读配置（文件读取属宿主职责，框架只解析 JSON 值；CS-§9 fail-fast）。
    const std::string config_path = resolve_config_path();
    if (!godot::FileAccess::file_exists(godot::String(config_path.c_str()))) {
        godot::UtilityFunctions::push_error("NPC 配置缺失: ", config_path.c_str());
        return false;
    }
    const godot::String file_text =
        godot::FileAccess::get_file_as_string(godot::String(config_path.c_str()));
    const std::string file_utf8 = file_text.utf8().get_data();
    // GDExtension 契约：扩展以 -fno-exceptions 编译，JSON 解析用非抛接口（CS-§9 fail-fast）。
    const nlohmann::json cfg_json = nlohmann::json::parse(file_utf8, nullptr, false);
    if (cfg_json.is_discarded()) {
        godot::UtilityFunctions::push_error("NPC 配置 JSON 解析失败: ", config_path.c_str());
        return false;
    }
    if (auto err = parse_agent_config(cfg_json, config_path, agent_cfg_); err.has_value()) {
        // 显式限定：避免成员查找命中 godot::Object::to_string()（成员优先于 ADL）。
        godot::UtilityFunctions::push_error(
            "NPC 配置校验失败: ", godot::String(npc_agent::core::to_string(*err).c_str()));
        return false;
    }
    // 演示参数（extra.demo 段）解析：fail-fast 同上。
    if (auto err = parse_demo_config(agent_cfg_.extra, cfg_); err.has_value()) {
        godot::UtilityFunctions::push_error("NPC 配置校验失败: ", godot::String(err->c_str()));
        return false;
    }
    // 交叉校验（仅旧单 NPC 巡逻模式）：问候触发距离不得超过感知半径。
    if (cfg_.scene.npcs.empty() && !cfg_.fsm.enabled &&
        cfg_.greet.max_distance > agent_cfg_.perception.radius) {
        godot::UtilityFunctions::push_error(
            "NPC 配置校验失败: greet.max_distance 超过 perception.radius，问候将永不触发");
        return false;
    }
    return true;
}

bool NpcAgentDemoNode::setup_agents() {
    system_.set_current_world(world_);
    if (cfg_.scene.npcs.empty())
        return setup_single_npc();
    return setup_multi_npc();
}

bool NpcAgentDemoNode::setup_single_npc() {
    agent_ = &system_.create_agent(std::move(agent_cfg_), body_);
    if (cfg_.fsm.enabled) {
        // 阶段 2 模式（R7-10）：框架 FsmDecisionMaker + PerceptionModule。
        decision::FsmDefinition fsm_def;
        if (auto err = decision::parse_fsm_definition(cfg_.fsm.definition, fsm_def);
            err.has_value()) {
            godot::UtilityFunctions::push_error("FSM 定义校验失败: ", godot::String(err->c_str()));
            return false;
        }
        agent_->set_decision_maker(
            std::make_unique<decision::FsmDecisionMaker>(std::move(fsm_def)));
        capabilities::PerceptionModuleParams perception_params;
        perception_params.stimulus_window_seconds = cfg_.fsm.stimulus_window_seconds;
        agent_->register_capability(
            std::make_unique<capabilities::PerceptionModule>(perception_params));
        agent_->register_capability(std::make_unique<testing::MoveDoneCapability>());
        log_status("装配阶段 2 行为系统（FSM + 感知模块）");
    } else {
        // 旧演示模式：巡逻决策器 + 问候/惊吓/警戒能力（参数化，见 demo_config）。
        agent_->set_decision_maker(std::make_unique<GuardPatrolDecision>(cfg_.patrol));
        agent_->register_capability(std::make_unique<GreetCapability>(cfg_.greet));
        agent_->register_capability(std::make_unique<StartleCapability>(cfg_.startle));
        agent_->register_capability(std::make_unique<AlertCapability>(cfg_.alert));
    }
    return true;
}

bool NpcAgentDemoNode::setup_multi_npc() {
    for (std::size_t i = 0; i < npcs_.size(); ++i) {
        auto& npc = npcs_[i];
        const NpcSpec& spec = cfg_.scene.npcs[i];

        // 每个 NPC 独立 Agent 配置（id 即规格名，感知半径 0：刺激驱动）。
        core::AgentConfig cfg;
        cfg.id = spec.name;
        cfg.decision_kind = "fsm";
        cfg.rng_seed = spec.rng_seed;
        cfg.perception.radius = 0.0f;

        npc.agent = &system_.create_agent(std::move(cfg), npc.body);
        npc.shout_when_say = spec.shout_when_say;
        npc.player_near_distance = spec.fsm.player_near_distance;
        npc.label = spec.label; // 中文显示名（名牌/面板）

        decision::FsmDefinition fsm_def;
        if (auto err = decision::parse_fsm_definition(spec.fsm.definition, fsm_def);
            err.has_value()) {
            godot::UtilityFunctions::push_error("FSM 定义校验失败 (",
                                                godot::String(spec.name.c_str()),
                                                "): ", godot::String(err->c_str()));
            return false;
        }
        npc.agent->set_decision_maker(
            std::make_unique<decision::FsmDecisionMaker>(std::move(fsm_def)));
        capabilities::PerceptionModuleParams perception_params;
        perception_params.stimulus_window_seconds = spec.fsm.stimulus_window_seconds;
        npc.agent->register_capability(
            std::make_unique<capabilities::PerceptionModule>(perception_params));
        npc.agent->register_capability(std::make_unique<testing::MoveDoneCapability>());
    }
    log_status("装配阶段 3 多 NPC 行为系统（每 NPC 独立 FSM + 感知 + 导航）");
    return true;
}

void NpcAgentDemoNode::build_scene() {
    // 窗口与坐标：窗口尺寸/缩放/出生点全部来自配置（软渲染机器可调小窗口）。
    godot::DisplayServer::get_singleton()->window_set_size(
        godot::Vector2i(cfg_.scene.window_width, cfg_.scene.window_height));
    const WorldTransform transform{
        cfg_.scene.scale, godot::Vector2(static_cast<float>(cfg_.scene.window_width) / 2.0f,
                                         static_cast<float>(cfg_.scene.window_height) / 2.0f)};
    world_.set_transform(transform);

    // 阶段 3 宿主导航：网格覆盖整个世界范围（世界原点=窗口中心，坐标为负，
    // GridNav 以 origin 支持）。单元 0.25 世界单位。
    const float world_width = static_cast<float>(cfg_.scene.window_width) / cfg_.scene.scale;
    const float world_height = static_cast<float>(cfg_.scene.window_height) / cfg_.scene.scale;
    constexpr float kCell = 0.25f;
    grid_ = testing::GridNav(static_cast<int>(world_width / kCell),
                             static_cast<int>(world_height / kCell), kCell);
    grid_.set_origin(Vec3{-world_width / 2.0f, -world_height / 2.0f, 0.0f});
    world_.set_grid_nav(&grid_);

    if (cfg_.scene.map_enabled) {
        draw_map(cfg_.scene.window_width, cfg_.scene.window_height);
        register_map_obstacles(cfg_.scene.window_width, cfg_.scene.window_height);
    }

    if (cfg_.scene.npcs.empty())
        build_single_npc_scene(transform);
    else
        build_multi_npc_scene(transform);

    // 玩家：精灵 + GDScript 输入脚本（跨语言边界：GDScript 回调本节点方法）；
    // 速度/边界参数经脚本变量注入。
    player_node_ = memnew(godot::Node2D);
    player_node_->set_name("Player");
    player_node_->set_position(transform.to_pixel(cfg_.scene.player_spawn));
    godot::Ref<godot::Script> player_script =
        godot::ResourceLoader::get_singleton()->load(kPlayerScriptPath);
    if (player_script.is_valid()) {
        player_node_->set_script(player_script); // 先挂脚本再入树（_ready 时序）
        player_node_->set("speed", cfg_.player.speed);
        player_node_->set("clamp_margin", cfg_.player.clamp_margin);
        // 钳制边界用配置窗口尺寸（无头模式首帧视口未初始化，不可读视口）。
        player_node_->set("clamp_size",
                          godot::Vector2(static_cast<float>(cfg_.scene.window_width),
                                         static_cast<float>(cfg_.scene.window_height)));
    }
    auto* player_sprite = memnew(godot::Sprite2D);
    player_sprite->set_texture(godot::ResourceLoader::get_singleton()->load(kPlayerSpritePath));
    player_sprite->set_scale(godot::Vector2(0.75f, 0.75f));
    player_node_->add_child(player_sprite);
    // 玩家头顶反馈气泡（E 放置结果提示：成功/无法放置）。
    player_bubble_ = memnew(godot::Label);
    player_bubble_->set_position(godot::Vector2(-90.0f, -84.0f));
    player_bubble_->set_size(godot::Vector2(180.0f, 0.0f));
    player_bubble_->set_horizontal_alignment(
        godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
    player_bubble_->add_theme_font_size_override("font_size", 14);
    player_bubble_->set_modulate(godot::Color(1.0f, 0.9f, 0.4f, 1.0f));
    player_bubble_->set_visible(false);
    player_node_->add_child(player_bubble_);
    add_child(player_node_);
    world_.add_entity("player", player_node_);

    // 调试面板 + 操作提示（开关来自配置）。
    debug_label_ = memnew(godot::Label);
    debug_label_->set_name("DebugLabel");
    debug_label_->set_position(godot::Vector2(16.0f, 16.0f));
    debug_label_->set_visible(cfg_.scene.show_debug);
    add_child(debug_label_);
    auto* hint_label = memnew(godot::Label);
    hint_label->set_name("HintLabel");
    hint_label->set_position(
        godot::Vector2(16.0f, static_cast<float>(cfg_.scene.window_height) - 30.0f));
    const char* hint = kHintText;
    if (!cfg_.scene.npcs.empty())
        hint = kHintTextMulti;
    else if (cfg_.fsm.enabled)
        hint = kHintTextFsm;
    hint_label->set_text(godot::String::utf8(hint));
    hint_label->set_modulate(godot::Color(0.7f, 0.7f, 0.7f, 1.0f));
    hint_label->set_visible(cfg_.scene.show_hint);
    add_child(hint_label);
}

void NpcAgentDemoNode::build_single_npc_scene(const WorldTransform& transform) {
    // NPC：精灵 + 头顶气泡（默认隐藏，台词/表情时限时显示）。
    npc_node_ = memnew(godot::Node2D);
    npc_node_->set_name("Npc");
    npc_node_->set_position(transform.to_pixel(cfg_.scene.npc_spawn));
    add_child(npc_node_);
    auto* npc_sprite = memnew(godot::Sprite2D);
    npc_sprite->set_texture(godot::ResourceLoader::get_singleton()->load(kNpcSpritePath));
    npc_node_->add_child(npc_sprite);
    bubble_label_ = memnew(godot::Label);
    bubble_label_->set_name("BubbleLabel");
    bubble_label_->set_position(godot::Vector2(-160.0f, -72.0f));
    bubble_label_->set_size(godot::Vector2(320.0f, 0.0f));
    bubble_label_->set_horizontal_alignment(
        godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
    bubble_label_->set_visible(false);
    npc_node_->add_child(bubble_label_);

    body_.bind(npc_node_, npc_sprite, bubble_label_, transform, cfg_.body);
}

void NpcAgentDemoNode::build_multi_npc_scene(const WorldTransform& transform) {
    for (const auto& spec : cfg_.scene.npcs) {
        NpcInstance npc;
        npc.node = memnew(godot::Node2D);
        npc.node->set_name(godot::String(spec.name.c_str()));
        npc.node->set_position(transform.to_pixel(spec.spawn));
        add_child(npc.node);
        npc.sprite = memnew(godot::Sprite2D);
        npc.sprite->set_texture(
            godot::ResourceLoader::get_singleton()->load(godot::String(spec.sprite.c_str())));
        npc.sprite->set_modulate(godot::Color(spec.tint[0], spec.tint[1], spec.tint[2], 1.0f));
        npc.node->add_child(npc.sprite);
        // 名牌：常驻中文小字，贴头顶（气泡之上；造型 + 名牌双重区分，R7-12）。
        auto* name_label = memnew(godot::Label);
        name_label->set_position(godot::Vector2(-60.0f, -90.0f));
        name_label->set_size(godot::Vector2(120.0f, 0.0f));
        name_label->set_horizontal_alignment(
            godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
        name_label->set_text(godot::String::utf8(spec.label.c_str()));
        name_label->add_theme_font_size_override("font_size", 13);
        name_label->set_modulate(godot::Color(1.0f, 1.0f, 1.0f, 0.95f)); // 白字可读
        npc.node->add_child(name_label);
        npc.bubble = memnew(godot::Label);
        npc.bubble->set_position(godot::Vector2(-160.0f, -72.0f));
        npc.bubble->set_size(godot::Vector2(320.0f, 0.0f));
        npc.bubble->set_horizontal_alignment(
            godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
        npc.bubble->set_visible(false);
        npc.node->add_child(npc.bubble);
        npc.body.bind(npc.node, npc.sprite, npc.bubble, transform, cfg_.body);
        // 碰撞检查（R10）：移动不得进入阻塞单元（建筑/木箱/树木）或穿越其他
        // 智能体（NPC 互不穿透 + 不穿玩家；self_node 排除自身）。
        npc.body.set_blocked_check([this, self_node = npc.node](Vec3 world) {
            const auto cell = grid_.world_to_cell(world);
            if (cell.has_value() && grid_.is_blocked(cell->first, cell->second))
                return true;
            for (const auto& other : npcs_) {
                if (other.node == self_node)
                    continue;
                const Vec3 p = other.body.body_state().position;
                const float dx = world.x - p.x;
                const float dy = world.y - p.y;
                if (dx * dx + dy * dy < kAgentRadius * kAgentRadius)
                    return true;
            }
            if (const auto pp = world_.entity_pos("player"); pp.has_value()) {
                const float dx = world.x - pp->x;
                const float dy = world.y - pp->y;
                if (dx * dx + dy * dy < kAgentRadius * kAgentRadius)
                    return true;
            }
            return false;
        });
        // 路线可视化节点：最多 32 个航点蓝点 + 1 个目标亮框。
        constexpr std::size_t kMaxDots = 32;
        npc.path_dots.reserve(kMaxDots);
        for (std::size_t i = 0; i < kMaxDots; ++i) {
            auto* dot = memnew(godot::ColorRect);
            dot->set_size(godot::Vector2(8.0f, 8.0f));
            dot->set_color(godot::Color(0.4f, 0.8f, 1.0f, 0.9f));
            dot->set_visible(false);
            add_child(dot);
            npc.path_dots.push_back(dot);
        }
        npc.target_marker = memnew(godot::ColorRect);
        npc.target_marker->set_size(godot::Vector2(14.0f, 14.0f));
        npc.target_marker->set_color(godot::Color(1.0f, 0.5f, 0.2f, 1.0f));
        npc.target_marker->set_visible(false);
        add_child(npc.target_marker);
        npcs_.push_back(std::move(npc));
    }
}

void NpcAgentDemoNode::draw_map(int width, int height) {
    // 装饰地图（R7-12）：纯视觉分层；建筑为真实障碍（register_map_obstacles）。
    const float center_x = static_cast<float>(width) / 2.0f;
    const float center_y = static_cast<float>(height) / 2.0f;
    const float scale = cfg_.scene.scale;

    auto* ground = memnew(godot::ColorRect);
    ground->set_position(godot::Vector2(0.0f, 0.0f));
    ground->set_size(godot::Vector2(static_cast<float>(width), static_cast<float>(height)));
    ground->set_color(godot::Color(0.16f, 0.24f, 0.14f, 1.0f)); // 草地
    add_child(ground);

    auto* road = memnew(godot::ColorRect);
    road->set_position(godot::Vector2(0.0f, static_cast<float>(height) * 0.62f));
    road->set_size(godot::Vector2(static_cast<float>(width), static_cast<float>(height) * 0.18f));
    road->set_color(godot::Color(0.42f, 0.40f, 0.36f, 1.0f)); // 道路
    add_child(road);
    auto* road_line = memnew(godot::ColorRect);
    road_line->set_position(godot::Vector2(0.0f, static_cast<float>(height) * 0.705f));
    road_line->set_size(godot::Vector2(static_cast<float>(width), 3.0f));
    road_line->set_color(godot::Color(0.85f, 0.83f, 0.55f, 1.0f)); // 中线
    add_child(road_line);

    // 建筑：世界坐标 → 像素（与 register_map_obstacles 同一张表，位置自适应窗口）。
    for (const auto& b : kBuildings) {
        auto* building = memnew(godot::ColorRect);
        building->set_position(godot::Vector2(center_x + b.x * scale, center_y + b.y * scale));
        building->set_size(godot::Vector2(b.w * scale, b.h * scale));
        building->set_color(b.color);
        add_child(building);
    }

    // 树木：八边形树冠 + 树桩（世界坐标；树心注册为障碍，见 register_map_obstacles）。
    for (const auto& tree : kTreePos) {
        const float tx = center_x + tree.x * scale;
        const float ty = center_y + tree.y * scale;
        auto* crown = memnew(godot::Polygon2D);
        godot::PackedVector2Array points;
        constexpr int kSides = 8;
        for (int s = 0; s < kSides; ++s) {
            const double angle = 2.0 * 3.141592653589793 * s / kSides;
            points.push_back(godot::Vector2(tx + static_cast<float>(std::cos(angle)) * 26.0f,
                                            ty + static_cast<float>(std::sin(angle)) * 26.0f));
        }
        crown->set_polygon(points);
        crown->set_color(godot::Color(0.14f, 0.45f, 0.16f, 1.0f));
        add_child(crown);
        auto* trunk = memnew(godot::ColorRect);
        trunk->set_position(godot::Vector2(tx - 4.0f, ty + 22.0f));
        trunk->set_size(godot::Vector2(8.0f, 14.0f));
        trunk->set_color(godot::Color(0.38f, 0.27f, 0.16f, 1.0f));
        add_child(trunk);
    }
}

void NpcAgentDemoNode::inject_player_flags(core::Agent& agent, float near_distance) {
    const auto player_pos = world_.entity_pos("player");
    if (!player_pos.has_value())
        return;
    const Vec3 npc_pos = agent.body_state().position;
    const float dx = player_pos->x - npc_pos.x;
    const float dy = player_pos->y - npc_pos.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    agent.blackboard().set("player_distance", distance);
    if (near_distance >= 0.0f) {
        // FSM 条件旗标：玩家进入近距阈值（问候状态迁移条件）。
        agent.blackboard().set("player_near", distance <= near_distance);
    }
}

void NpcAgentDemoNode::propagate_shouts() {
    // 宿主声学传播：NPC 的"呼叫支援"台词出现时广播 stimulus.shout（边沿触发，
    // 台词离开后复位），其余 NPC 经感知模块 heard_shout 响应（R7-11 连锁反应）。
    for (auto& npc : npcs_) {
        if (!npc.shout_when_say)
            continue;
        bool shouting = false;
        if (npc.agent->last_intent().has_value()) {
            const auto& intent = *npc.agent->last_intent();
            if (std::holds_alternative<SayIntent>(intent.payload)) {
                const std::string& text = std::get<SayIntent>(intent.payload).text;
                shouting = text.find(kShoutMarker) != std::string::npos;
            }
        }
        if (shouting && !npc.shout_sent) {
            const Vec3 pos = npc.agent->body_state().position;
            Stimulus shout;
            shout.type = "shout";
            shout.position = pos;
            shout.magnitude = 2.0f;
            shout.source_id = std::string(npc.agent->id());
            system_.inject_stimulus(shout);
            npc.shout_sent = true;
        } else if (!shouting) {
            npc.shout_sent = false;
        }
    }
}

void NpcAgentDemoNode::register_map_obstacles(int width, int height) {
    // 建筑（世界坐标）→ 网格单元阻塞（与 draw_map 同一张表，R10）。
    (void)width;
    (void)height;
    const auto register_rect = [this](const MapBuilding& b) {
        const auto min_cell = grid_.world_to_cell(Vec3{b.x, b.y, 0.0f});
        const auto max_cell = grid_.world_to_cell(Vec3{b.x + b.w, b.y + b.h, 0.0f});
        if (!min_cell.has_value() || !max_cell.has_value())
            return;
        grid_.block_rect(min_cell->first, min_cell->second, max_cell->first, max_cell->second);
    };
    for (const auto& b : kBuildings)
        register_rect(b);
    for (const auto& b : kPond)
        register_rect(b);
    for (const auto& b : kFence)
        register_rect(b);
    // 树木：树心单单元阻塞（树冠半径 0.26u ≈ 单元 0.25u，不可穿过）。
    for (const auto& t : kTreePos) {
        const auto cell = grid_.world_to_cell(t);
        if (!cell.has_value())
            continue;
        grid_.block_rect(cell->first, cell->second, cell->first, cell->second);
    }
}

std::vector<Vec3> NpcAgentDemoNode::find_path_avoiding(const NpcInstance& self, Vec3 from,
                                                       Vec3 to) {
    // 动态避障（R10-12）：临时把其他 NPC 与玩家的当前位置阻塞为障碍，求路径后
    // 恢复。这样 A* 路径不会穿过正在移动的智能体——两 NPC 路径交叉相遇时，
    // 双方都绕开对方当前位置，而不是 blocked 互等卡死。
    struct TempBlock {
        int col;
        int row;
        bool was_blocked;
    };
    std::vector<TempBlock> temp;
    // 障碍膨胀为 3×3 单元（0.75u，覆盖 kAgentRadius=0.35u 半径）：A* 路径从
    // 智能体旁至少 0.375u 经过，blocked_check（0.35u）不会再挡绕行路径。
    const auto block_around = [this, &temp](const Vec3& p) {
        const auto cell = grid_.world_to_cell(p);
        if (!cell.has_value())
            return;
        for (int dc = -1; dc <= 1; ++dc) {
            for (int dr = -1; dr <= 1; ++dr) {
                const int c = cell->first + dc;
                const int r = cell->second + dr;
                if (!grid_.in_bounds(c, r))
                    continue;
                temp.push_back({c, r, grid_.is_blocked(c, r)});
            }
        }
    };
    for (const auto& other : npcs_) {
        if (&other == &self)
            continue;
        block_around(other.body.body_state().position);
    }
    if (const auto pp = world_.entity_pos("player"); pp.has_value())
        block_around(*pp);
    for (const auto& t : temp)
        grid_.set_obstacle(t.col, t.row, true);
    auto path = world_.find_path(from, to);
    for (const auto& t : temp)
        grid_.set_obstacle(t.col, t.row, t.was_blocked);
    return path;
}

void NpcAgentDemoNode::plan_paths_and_report() {
    // 阶段 3（R10）：新移动意图 → 经动态避障寻路（GridNav A*，绕开其他 NPC/玩家）
    // 注入航点，并刷新路线可视化。到达回投见 _process（tick 前消费，R10 修复——
    // FSM 每 tick 重发同一移动意图会经 execute→move_to 重置到达标志）。
    for (auto& npc : npcs_) {
        const auto& intent = npc.agent->last_intent();
        if (intent.has_value() && std::holds_alternative<MoveIntent>(intent->payload)) {
            const auto& move = std::get<MoveIntent>(intent->payload);
            const auto path =
                find_path_avoiding(npc, npc.agent->body_state().position, move.target);
            if (!path.empty())
                npc.body.set_path(path, move.speed);
            update_path_visual(npc, path);
        } else {
            update_path_visual(npc, npc.body.is_moving() ? npc.body.path() : std::vector<Vec3>{});
        }
    }
}

void NpcAgentDemoNode::update_path_visual(NpcInstance& npc, const std::vector<Vec3>& path) {
    // 路线可视化（R10）：航点蓝点 + 目标亮框，让寻路/绕行肉眼可见。
    const godot::Vector2 center(static_cast<float>(cfg_.scene.window_width) / 2.0f,
                                static_cast<float>(cfg_.scene.window_height) / 2.0f);
    for (std::size_t i = 0; i < npc.path_dots.size(); ++i) {
        if (i < path.size()) {
            const godot::Vector2 px =
                center + godot::Vector2(path[i].x, path[i].y) * cfg_.scene.scale;
            npc.path_dots[i]->set_position(px - godot::Vector2(4.0f, 4.0f));
            npc.path_dots[i]->set_visible(true);
        } else {
            npc.path_dots[i]->set_visible(false);
        }
    }
    if (npc.target_marker != nullptr) {
        if (!path.empty()) {
            const godot::Vector2 px =
                center + godot::Vector2(path.back().x, path.back().y) * cfg_.scene.scale;
            npc.target_marker->set_position(px - godot::Vector2(7.0f, 7.0f));
            npc.target_marker->set_visible(true);
        } else {
            npc.target_marker->set_visible(false);
        }
    }
}

void NpcAgentDemoNode::place_obstacle(float dir_x, float dir_y) {
    if (!ready_ || npcs_.empty())
        return;
    // E 键限频（6 帧 ≈ 0.1s@60fps）：只防 key repeat 每帧重复放置（否则按住 E
    // 瞬间铺满），同时保留对快速连按的响应（人手最快 ~8 次/秒 < 0.1s 间隔）。
    const uint64_t now = godot::Engine::get_singleton()->get_process_frames();
    if (now - last_place_ms_ < 6)
        return;
    last_place_ms_ = now;
    const auto player_pos = world_.entity_pos("player");
    if (!player_pos.has_value())
        return;
    const auto cell = grid_.world_to_cell(*player_pos);
    if (!cell.has_value())
        return;
    // 放置方向：玩家最近移动方向（GDScript 传入），零向量兜底向右。
    // 木箱放在前方 2 单元处的 2×2 块，与玩家隔 1 单元空隙——不压脚下、不卡死角。
    int dx = dir_x > 0.0f ? 1 : (dir_x < 0.0f ? -1 : 0);
    int dy = dir_y > 0.0f ? 1 : (dir_y < 0.0f ? -1 : 0);
    if (dx == 0 && dy == 0)
        dx = 1;
    // 候选方向：前 → 顺时针 → 逆时针 → 反（前方被堵时退而求其次，避免围死自己）。
    // 每个方向在 2→4 单元（0.5~1.0 世界单位）内找第一个可放置的 2×2 块：
    // 木箱始终落在按 E 的附近（不超过 1u），连续按 E 会沿视线向前排布；
    // 该距离内放满则换方向。绝不放到 1u 之外（宁可提示无法放置）。
    const std::pair<int, int> kCands[] = {{dx, dy}, {-dy, dx}, {dy, -dx}, {-dx, -dy}};
    int px0 = 0, py0 = 0;
    bool placed = false;
    for (const auto& [cx, cy] : kCands) {
        for (int dist = 2; dist <= 4 && !placed; ++dist) {
            const int x0 = cell->first + cx * dist;
            const int y0 = cell->second + cy * dist;
            bool ok = true;
            for (int ox = 0; ox < 2 && ok; ++ox)
                for (int oy = 0; oy < 2 && ok; ++oy)
                    if (!grid_.in_bounds(x0 + ox, y0 + oy) || grid_.is_blocked(x0 + ox, y0 + oy))
                        ok = false;
            if (ok) {
                px0 = x0;
                py0 = y0;
                placed = true;
            }
        }
        if (placed)
            break;
    }
    if (!placed) {
        // 视觉反馈（窗口里玩家看不到控制台）：头顶气泡提示，0.9s 后消失。
        if (player_bubble_ != nullptr) {
            player_bubble_->set_text(godot::String::utf8("四周都被挡住，无法放置"));
            player_bubble_->set_visible(true);
            player_bubble_left_ = 0.9;
        }
        godot::UtilityFunctions::print(godot::String::utf8("[demo] 四周都被挡住，无法放置木箱"));
        return;
    }
    // 放置成功：网格阻塞 2×2 单元 + 视觉方块 + 头顶气泡确认（0.5s）。
    grid_.block_rect(px0, py0, px0 + 1, py0 + 1);
    if (player_bubble_ != nullptr) {
        player_bubble_->set_text(godot::String::utf8("已放置木箱"));
        player_bubble_->set_visible(true);
        player_bubble_left_ = 0.5;
    }
    const float px_per_cell = cfg_.scene.scale * grid_.cell_size();
    const godot::Vector2 center(static_cast<float>(cfg_.scene.window_width) / 2.0f,
                                static_cast<float>(cfg_.scene.window_height) / 2.0f);
    // 像素位置 = 窗口中心 + 单元中心世界坐标 × 缩放（必须含网格 origin 偏移，
    // 否则木箱会画到窗口外——R10-13 修复）。
    const Vec3 w0 = grid_.cell_to_world(px0, py0);
    auto* box = memnew(godot::ColorRect);
    box->set_position(center + godot::Vector2(w0.x * cfg_.scene.scale, w0.y * cfg_.scene.scale));
    box->set_size(godot::Vector2(px_per_cell * 2.0f, px_per_cell * 2.0f));
    box->set_color(godot::Color(0.62f, 0.47f, 0.25f, 1.0f));
    add_child(box);
    // 途中障碍触发重规划：移动中的 NPC 立即重新寻路到原目标（同样绕开其他智能体）。
    for (auto& npc : npcs_) {
        if (!npc.body.is_moving())
            continue;
        const auto path =
            find_path_avoiding(npc, npc.agent->body_state().position, npc.body.path_target());
        if (!path.empty())
            npc.body.set_path(path, npc.body.move_speed());
    }
}

bool NpcAgentDemoNode::is_pixel_blocked(float px, float py) {
    if (!ready_)
        return false;
    // 像素 → 世界 → 网格单元阻塞判定（player.gd 移动前调用，碰撞语义）。
    const godot::Vector2 center(static_cast<float>(cfg_.scene.window_width) / 2.0f,
                                static_cast<float>(cfg_.scene.window_height) / 2.0f);
    const Vec3 world{(px - center.x) / cfg_.scene.scale, (py - center.y) / cfg_.scene.scale, 0.0f};
    const auto cell = grid_.world_to_cell(world);
    if (cell.has_value() && grid_.is_blocked(cell->first, cell->second))
        return true;
    // 玩家同样不穿越 NPC（动态占据，与 NPC blocked_check 同一半径）。
    for (const auto& npc : npcs_) {
        const Vec3 p = npc.body.body_state().position;
        const float dx = world.x - p.x;
        const float dy = world.y - p.y;
        if (dx * dx + dy * dy < kAgentRadius * kAgentRadius)
            return true;
    }
    return false;
}

void NpcAgentDemoNode::update_debug_label() {
    const TickContext tc = world_.tick_context();
    std::string text =
        "tick " + std::to_string(tc.tick_index) + "  t=" + std::to_string(tc.game_time) + "s";
    if (npcs_.empty()) {
        nlohmann::json bb;
        agent_->blackboard().to_json(bb);
        text += "\n" + std::string(agent_->id()) +
                ": intent: " + testing::describe_intent(agent_->last_intent()) + "\n" + bb.dump();
    } else {
        for (const auto& npc : npcs_) {
            text += "\n" + npc.label + ": " + testing::describe_intent(npc.agent->last_intent());
        }
    }
    debug_label_->set_text(godot::String::utf8(text.c_str()));
}

void NpcAgentDemoNode::log_status(const std::string& msg) {
    godot::UtilityFunctions::print(godot::String::utf8(msg.c_str()));
}

} // namespace npc_agent::adapter::godot_demo
