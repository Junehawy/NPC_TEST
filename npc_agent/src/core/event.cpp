// AgentEvent 的 nlohmann ADL 序列化（trace 输出用；事件本身不参与存档，RA-§4）。
#include "npc_agent/core/event.h"

namespace npc_agent::core {

void to_json(nlohmann::json& out, const AgentEvent& e) {
    out = nlohmann::json{
        {"type", e.type}, {"source", e.source}, {"payload", e.payload}, {"game_time", e.game_time}};
}

void from_json(const nlohmann::json& in, AgentEvent& e) {
    if (!in.is_object())
        return; // 前置：结构由调用方保证（CS-§9）
    e.type = in.value("type", "");
    e.source = in.value("source", "");
    e.payload = in.value("payload", nlohmann::json::object());
    e.game_time = in.value("game_time", 0.0);
}

} // namespace npc_agent::core
