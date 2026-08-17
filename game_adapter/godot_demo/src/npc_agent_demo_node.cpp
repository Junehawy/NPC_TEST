#include "npc_agent_demo_node.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <godot_cpp/classes/color_rect.hpp>
#include <godot_cpp/classes/display_server.hpp>
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
#include "npc_agent/testing/intent_desc.h"
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
    "阶段 2 多NPC：WASD 移动 · 空格 枪声 → 守卫警戒呼叫支援 · 平民逃窜 · 支援兵响应 · 靠近问候";

constexpr const char* kShoutMarker = "呼叫支援"; // 台词标记（宿主声学传播触发条件）
} // namespace

void NpcAgentDemoNode::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("inject_gunshot"),
                                &NpcAgentDemoNode::inject_gunshot);
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
    system_.tick();
    if (!npcs_.empty())
        propagate_shouts(); // 呼叫支援台词 → stimulus.shout（连锁反应）
    update_debug_label();   // 演示规模：每帧刷新面板，保证瞬时意图可见
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
    }
    log_status("装配阶段 2 多 NPC 行为系统（每 NPC 独立 FSM + 感知模块）");
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

    if (cfg_.scene.map_enabled)
        draw_map(cfg_.scene.window_width, cfg_.scene.window_height);

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
    player_node_->add_child(player_sprite);
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
        // 名牌：常驻小字（造型 + 名牌双重区分，R7-12）。
        auto* name_label = memnew(godot::Label);
        name_label->set_position(godot::Vector2(-80.0f, -104.0f));
        name_label->set_size(godot::Vector2(160.0f, 0.0f));
        name_label->set_horizontal_alignment(
            godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
        name_label->set_text(godot::String(spec.name.c_str()));
        name_label->add_theme_font_size_override("font_size", 14);
        name_label->set_modulate(godot::Color(spec.tint[0], spec.tint[1], spec.tint[2], 1.0f));
        npc.node->add_child(name_label);
        npc.bubble = memnew(godot::Label);
        npc.bubble->set_position(godot::Vector2(-160.0f, -72.0f));
        npc.bubble->set_size(godot::Vector2(320.0f, 0.0f));
        npc.bubble->set_horizontal_alignment(
            godot::HorizontalAlignment::HORIZONTAL_ALIGNMENT_CENTER);
        npc.bubble->set_visible(false);
        npc.node->add_child(npc.bubble);
        npc.body.bind(npc.node, npc.sprite, npc.bubble, transform, cfg_.body);
        npcs_.push_back(std::move(npc));
    }
}

void NpcAgentDemoNode::draw_map(int width, int height) {
    // 装饰地图（R7-12）：纯视觉分层，无碰撞（碰撞/寻路属阶段 3）。
    // 建筑与树木布置在边缘/上侧，避开 NPC 巡逻路径（中央与横向路线）。
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

    // 建筑（左上/右上/左下/右下 + 中上塔楼）。
    const struct {
        float x;
        float y;
        float w;
        float h;
        godot::Color color;
    } kBuildings[] = {
        {40.0f, 50.0f, 140.0f, 120.0f, godot::Color(0.55f, 0.35f, 0.25f, 1.0f)},
        {700.0f, 40.0f, 180.0f, 110.0f, godot::Color(0.45f, 0.45f, 0.52f, 1.0f)},
        {60.0f, 380.0f, 150.0f, 100.0f, godot::Color(0.62f, 0.52f, 0.30f, 1.0f)},
        {760.0f, 400.0f, 140.0f, 90.0f, godot::Color(0.50f, 0.32f, 0.28f, 1.0f)},
        {420.0f, 55.0f, 110.0f, 80.0f, godot::Color(0.36f, 0.36f, 0.40f, 1.0f)},
    };
    for (const auto& b : kBuildings) {
        auto* building = memnew(godot::ColorRect);
        building->set_position(godot::Vector2(b.x, b.y));
        building->set_size(godot::Vector2(b.w, b.h));
        building->set_color(b.color);
        add_child(building);
    }

    // 树木：八边形树冠 + 树桩。
    const float kTreeX[] = {240.0f, 640.0f, 820.0f, 100.0f, 520.0f};
    const float kTreeY[] = {90.0f, 110.0f, 150.0f, 475.0f, 470.0f};
    for (std::size_t i = 0; i < std::size(kTreeX); ++i) {
        auto* crown = memnew(godot::Polygon2D);
        godot::PackedVector2Array points;
        constexpr int kSides = 8;
        for (int s = 0; s < kSides; ++s) {
            const double angle = 2.0 * 3.141592653589793 * s / kSides;
            points.push_back(
                godot::Vector2(kTreeX[i] + static_cast<float>(std::cos(angle)) * 26.0f,
                               kTreeY[i] + static_cast<float>(std::sin(angle)) * 26.0f));
        }
        crown->set_polygon(points);
        crown->set_color(godot::Color(0.14f, 0.45f, 0.16f, 1.0f));
        add_child(crown);
        auto* trunk = memnew(godot::ColorRect);
        trunk->set_position(godot::Vector2(kTreeX[i] - 4.0f, kTreeY[i] + 22.0f));
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
            text += "\n" + std::string(npc.agent->id()) + ": " +
                    testing::describe_intent(npc.agent->last_intent());
        }
    }
    debug_label_->set_text(godot::String::utf8(text.c_str()));
}

void NpcAgentDemoNode::log_status(const std::string& msg) {
    godot::UtilityFunctions::print(godot::String::utf8(msg.c_str()));
}

} // namespace npc_agent::adapter::godot_demo
