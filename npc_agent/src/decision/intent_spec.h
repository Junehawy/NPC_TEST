// intent_spec —— 意图模板解析（decision/ 模块内部共享件，非公共 API）。
// 与 FSM/Utility/BT 的意图定义共用同一 schema 与 fail-fast 错误语义。
#pragma once

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "npc_agent/interfaces/intent.h"

namespace npc_agent::decision::internal {

// 意图模板解析（kind 分发，fail-fast）：
// move_to: {"kind":"move_to","target":[x,y,z],"speed":1.0}
// say:     {"kind":"say","text":"...","tone":"neutral"}
// emote:   {"kind":"emote","name":"..."}
// game_event: {"kind":"game_event","type":"...","payload":{...}}
// idle:    {"kind":"idle"} → out = nullopt（无意图，让位模块候选）
inline std::optional<std::string> parse_intent_spec(const nlohmann::json& spec,
                                                    std::optional<Intent>& out) {
    if (!spec.is_object())
        return std::string("intent 应为对象");
    const auto kind_it = spec.find("kind");
    if (kind_it == spec.end() || !kind_it->is_string())
        return std::string("intent.kind 应为字符串");
    const std::string kind = kind_it->get<std::string>();

    if (kind == "idle") {
        out = std::nullopt;
        return std::nullopt;
    }
    Intent intent;
    if (kind == "move_to") {
        const auto target_it = spec.find("target");
        if (target_it == spec.end() || !target_it->is_array() || target_it->size() != 3)
            return std::string("intent.target 应为 [x, y, z] 数组");
        MoveIntent move;
        move.target = Vec3{(*target_it)[0].get<float>(), (*target_it)[1].get<float>(),
                           (*target_it)[2].get<float>()};
        move.speed = spec.value("speed", 1.0f);
        intent.payload = move;
    } else if (kind == "say") {
        const auto text_it = spec.find("text");
        if (text_it == spec.end() || !text_it->is_string())
            return std::string("intent.text 应为字符串");
        SayIntent say;
        say.text = text_it->get<std::string>();
        say.tone = spec.value("tone", "neutral");
        intent.payload = say;
    } else if (kind == "emote") {
        const auto name_it = spec.find("name");
        if (name_it == spec.end() || !name_it->is_string())
            return std::string("intent.name 应为字符串");
        intent.payload = EmoteIntent{name_it->get<std::string>()};
    } else if (kind == "game_event") {
        const auto type_it = spec.find("type");
        if (type_it == spec.end() || !type_it->is_string())
            return std::string("intent.type 应为字符串");
        GameEvent event;
        event.type = type_it->get<std::string>();
        event.payload = spec.value("payload", nlohmann::json::object());
        intent.payload = GameEventIntent{std::move(event)};
    } else {
        return std::string("intent.kind 未知: " + kind);
    }
    out = std::move(intent);
    return std::nullopt;
}

} // namespace npc_agent::decision::internal
