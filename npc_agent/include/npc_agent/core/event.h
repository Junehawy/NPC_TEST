// AgentEvent —— 事件数据契约（RA-§4）：瞬时通知，不参与序列化。
// scope 分层：全局事件（广播）与 Agent 私有事件（点对点）共用本结构，
// 分层由路由方式决定（AgentSystem 全局队列 / Agent 私有队列）。
#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace npc_agent::core {

struct AgentEvent {
    std::string type;   // 如 "stimulus.gunshot" / "action.completed"
    std::string source; // 来源 id（agent id 或 "world"/宿主实体 id）
    nlohmann::json payload = nlohmann::json::object();
    double game_time = 0.0; // 发生时刻（trace 回溯用）
};

void to_json(nlohmann::json& out, const AgentEvent& e);
void from_json(const nlohmann::json& in, AgentEvent& e);

} // namespace npc_agent::core
