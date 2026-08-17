#include "npc_agent_demo_node.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "alert_capability.h"
#include "greet_capability.h"
#include "guard_patrol_decision.h"
#include "npc_agent/core/agent_config.h"
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
    setup_agent();
    ready_ = true;
    log_status("NPC 智能体已就绪：" + std::string(agent_->id()));
}

void NpcAgentDemoNode::_process(double delta) {
    if (!ready_)
        return;
    world_.advance(delta); // 世界时间先于 tick 推进（与无头示例一致）
    // 警戒计时：到期解除黑板 alarm，决策器恢复巡逻（RA-§3.4 pending 兜底演示）。
    if (alarm_time_left_ > 0.0) {
        alarm_time_left_ -= delta;
        if (alarm_time_left_ <= 0.0)
            agent_->blackboard().set("alarm", false);
    }
    body_.update_movement(delta);
    body_.update_bubble(delta);
    inject_player_distance(); // 宿主注入派生状态（问候距离判定，黑板契约内）
    system_.tick();
    update_debug_label(); // 演示规模：每帧刷新面板，保证瞬时意图（惊吓）可见
}

void NpcAgentDemoNode::inject_gunshot() {
    if (!ready_)
        return;
    const auto source_pos = world_.entity_pos("player").value_or(Vec3{});
    system_.inject_stimulus(Stimulus{cfg_.startle.stimulus_type, source_pos, 1.0f, "player"});
    // 警戒：黑板置位使巡逻决策器返回 pending，能力候选接管仲裁（RA-§3.4）。
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
    // 交叉校验：问候触发距离不得超过感知半径（否则问候永不触发，属配置错误）。
    if (cfg_.greet.max_distance > agent_cfg_.perception.radius) {
        godot::UtilityFunctions::push_error(
            "NPC 配置校验失败: greet.max_distance 超过 perception.radius，问候将永不触发");
        return false;
    }
    return true;
}

void NpcAgentDemoNode::setup_agent() {
    // 系统装配：世界 → 创建 Agent 挂身体 → 决策器与能力（全部参数来自配置）。
    system_.set_current_world(world_);
    agent_ = &system_.create_agent(std::move(agent_cfg_), body_);
    agent_->set_decision_maker(std::make_unique<GuardPatrolDecision>(cfg_.patrol));
    agent_->register_capability(std::make_unique<GreetCapability>(cfg_.greet));
    agent_->register_capability(std::make_unique<StartleCapability>(cfg_.startle));
    agent_->register_capability(std::make_unique<AlertCapability>(cfg_.alert));
}

void NpcAgentDemoNode::build_scene() {
    // 窗口与坐标：窗口尺寸/缩放/出生点全部来自配置（软渲染机器可调小窗口）。
    godot::DisplayServer::get_singleton()->window_set_size(
        godot::Vector2i(cfg_.scene.window_width, cfg_.scene.window_height));
    const WorldTransform transform{
        cfg_.scene.scale, godot::Vector2(static_cast<float>(cfg_.scene.window_width) / 2.0f,
                                         static_cast<float>(cfg_.scene.window_height) / 2.0f)};
    world_.set_transform(transform);

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
    hint_label->set_text(godot::String::utf8(kHintText));
    hint_label->set_modulate(godot::Color(0.7f, 0.7f, 0.7f, 1.0f));
    hint_label->set_visible(cfg_.scene.show_hint);
    add_child(hint_label);

    body_.bind(npc_node_, npc_sprite, bubble_label_, transform, cfg_.body);
}

void NpcAgentDemoNode::inject_player_distance() {
    const auto player_pos = world_.entity_pos("player");
    if (!player_pos.has_value())
        return;
    const Vec3 npc_pos = body_.body_state().position;
    const float dx = player_pos->x - npc_pos.x;
    const float dy = player_pos->y - npc_pos.y;
    agent_->blackboard().set("player_distance", std::sqrt(dx * dx + dy * dy));
}

void NpcAgentDemoNode::update_debug_label() {
    nlohmann::json bb;
    agent_->blackboard().to_json(bb);
    const TickContext tc = world_.tick_context();
    const std::string text = "intent: " + testing::describe_intent(agent_->last_intent()) +
                             "\ntick " + std::to_string(tc.tick_index) +
                             "  t=" + std::to_string(tc.game_time) + "s\n" + bb.dump();
    debug_label_->set_text(godot::String::utf8(text.c_str()));
}

void NpcAgentDemoNode::log_status(const std::string& msg) {
    godot::UtilityFunctions::print(godot::String::utf8(msg.c_str()));
}

} // namespace npc_agent::adapter::godot_demo
