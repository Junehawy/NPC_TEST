#include "npc_agent_demo_node.h"

#include <memory>
#include <string>
#include <utility>

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/classes/sprite2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "npc_agent/core/agent_config.h"
#include "npc_agent/interfaces/intent.h"
#include "npc_agent/testing/intent_desc.h"
#include "npc_agent/testing/toy_capabilities.h"

namespace npc_agent::adapter::godot_demo {

namespace {
constexpr const char* kConfigPath = "res://assets/npcs/sample_guard.json";
constexpr const char* kPlayerScriptPath = "res://scripts/player.gd";
constexpr const char* kNpcSpritePath = "res://assets/sprites/npc.svg";
constexpr const char* kPlayerSpritePath = "res://assets/sprites/player.svg";
constexpr double kAlarmSeconds = 3.0; // 枪声后警戒时长（决策器 pending 窗口）

constexpr const char* kHintText =
    "WASD 移动 · 空格 枪声刺激（NPC 惊吓并警戒 3 秒）· 靠近 NPC 触发问候";
} // namespace

void NpcAgentDemoNode::_bind_methods() {
    godot::ClassDB::bind_method(godot::D_METHOD("inject_gunshot"),
                                &NpcAgentDemoNode::inject_gunshot);
}

void NpcAgentDemoNode::_ready() {
    build_scene();
    if (!setup_agent()) {
        ready_ = false;
        return;
    }
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
    system_.tick();
    update_debug_label(); // 演示规模：每帧刷新面板，保证瞬时意图（惊吓）可见
}

void NpcAgentDemoNode::inject_gunshot() {
    if (!ready_)
        return;
    const auto source_pos = world_.entity_pos("player").value_or(Vec3{});
    system_.inject_stimulus(Stimulus{"gunshot", source_pos, 1.0f, "player"});
    // 警戒：黑板置位使 ToyPatrolDecision 返回 pending，能力候选接管仲裁。
    agent_->blackboard().set("alarm", true);
    alarm_time_left_ = kAlarmSeconds;
}

void NpcAgentDemoNode::build_scene() {
    // 坐标约定：世界原点位于屏幕中心，1 世界单位 = 100 像素（1280×720 窗口）。
    const WorldTransform transform{100.0f, godot::Vector2(640.0f, 360.0f)};
    world_.set_transform(transform);

    // NPC：精灵 + 头顶气泡（默认隐藏，台词/表情时限时显示）。
    npc_node_ = memnew(godot::Node2D);
    npc_node_->set_name("Npc");
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

    // 玩家：精灵 + GDScript 输入脚本（跨语言边界：GDScript 回调本节点方法）。
    player_node_ = memnew(godot::Node2D);
    player_node_->set_name("Player");
    player_node_->set_position(transform.to_pixel(Vec3{3.5f, 0.0f, 0.0f}));
    godot::Ref<godot::Script> player_script =
        godot::ResourceLoader::get_singleton()->load(kPlayerScriptPath);
    if (player_script.is_valid())
        player_node_->set_script(player_script); // 先挂脚本再入树（_ready 时序）
    auto* player_sprite = memnew(godot::Sprite2D);
    player_sprite->set_texture(godot::ResourceLoader::get_singleton()->load(kPlayerSpritePath));
    player_node_->add_child(player_sprite);
    add_child(player_node_);
    world_.add_entity("player", player_node_);

    // 调试面板 + 操作提示。
    debug_label_ = memnew(godot::Label);
    debug_label_->set_name("DebugLabel");
    debug_label_->set_position(godot::Vector2(16.0f, 16.0f));
    add_child(debug_label_);
    auto* hint_label = memnew(godot::Label);
    hint_label->set_name("HintLabel");
    hint_label->set_position(godot::Vector2(16.0f, 690.0f));
    hint_label->set_text(godot::String::utf8(kHintText));
    hint_label->set_modulate(godot::Color(0.7f, 0.7f, 0.7f, 1.0f));
    add_child(hint_label);

    body_.bind(npc_node_, bubble_label_, transform);
}

bool NpcAgentDemoNode::setup_agent() {
    // 1. 读配置（文件读取属宿主职责，框架只解析 JSON 值；CS-§9 fail-fast）。
    if (!godot::FileAccess::file_exists(kConfigPath)) {
        godot::UtilityFunctions::push_error("NPC 配置缺失: ", kConfigPath);
        return false;
    }
    const godot::String file_text = godot::FileAccess::get_file_as_string(kConfigPath);
    const std::string file_utf8 = file_text.utf8().get_data();
    // GDExtension 契约：扩展以 -fno-exceptions 编译，JSON 解析用非抛接口（CS-§9 fail-fast）。
    const nlohmann::json cfg_json = nlohmann::json::parse(file_utf8, nullptr, false);
    if (cfg_json.is_discarded()) {
        godot::UtilityFunctions::push_error("NPC 配置 JSON 解析失败: ", kConfigPath);
        return false;
    }
    core::AgentConfig cfg;
    if (auto err = parse_agent_config(cfg_json, "sample_guard.json", cfg); err.has_value()) {
        // 显式限定：避免成员查找命中 godot::Object::to_string()（成员优先于 ADL）。
        godot::UtilityFunctions::push_error(
            "NPC 配置校验失败: ", godot::String(npc_agent::core::to_string(*err).c_str()));
        return false;
    }

    // 2. 系统装配：世界 → 创建 Agent 挂身体 → 决策器与能力（与无头示例同构）。
    system_.set_current_world(world_);
    agent_ = &system_.create_agent(std::move(cfg), body_);
    agent_->set_decision_maker(
        std::make_unique<testing::ToyPatrolDecision>(Vec3{0.0f, 0.0f, 0.0f}));
    agent_->register_capability(std::make_unique<testing::ToyGreetCapability>());
    agent_->register_capability(std::make_unique<testing::ToyStartleCapability>());
    return true;
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
