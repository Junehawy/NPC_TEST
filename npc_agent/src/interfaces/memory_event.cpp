// MemoryEvent 的 nlohmann ADL 序列化（RA-§7.1：往返对称，缺省字段取默认值）。
#include "npc_agent/interfaces/memory_event.h"

namespace npc_agent {

void to_json(nlohmann::json& out, const MemoryEvent& e) {
    out = nlohmann::json{
        {"subject", e.subject},     {"object", e.object},         {"type", e.type},
        {"timestamp", e.timestamp}, {"importance", e.importance}, {"payload", e.payload}};
}

void from_json(const nlohmann::json& in, MemoryEvent& e) {
    if (!in.is_object())
        return; // 前置：结构由调用方保证（CS-§9）
    e.subject = in.value("subject", "");
    e.object = in.value("object", "");
    e.type = in.value("type", "");
    e.timestamp = in.value("timestamp", 0.0);
    e.importance = in.value("importance", 0.0f);
    e.payload = in.value("payload", nlohmann::json::object());
}

} // namespace npc_agent
