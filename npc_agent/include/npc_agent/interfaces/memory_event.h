// 记忆事件数据契约（RA-§7.1，阶段 1 冻结）：带 subject/object 支持按主体/对象分区索引。
// 值语义；经 nlohmann ADL 序列化，往返对称（TS-§3 序列化测试覆盖）。
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace npc_agent {

struct MemoryEvent {
    std::string subject;     // 谁（主体 id）
    std::string object;      // 对谁/什么事（对象 id 或描述键）
    std::string type;        // 事件类型（枚举注册表管理，如 "heard_gunshot"）
    double timestamp = 0.0;  // game_time
    float importance = 0.0f; // 0~1，供摘要/检索排序
    nlohmann::json payload = nlohmann::json::object(); // 类型相关附加字段
};

// ADL 序列化：to_json/from_json 对称（缺省字段用默认值填充）。
void to_json(nlohmann::json& out, const MemoryEvent& e);
void from_json(const nlohmann::json& in, MemoryEvent& e);

} // namespace npc_agent
