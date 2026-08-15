// AgentConfig —— 单 NPC 配置解析（fail-fast，RA-§5.2 风格）。
// v1 字段：id（必填）、decision（记录用途，决策器实例由宿主/工厂注册）、
// rng_seed、perception（AgentSystem 代执行感知查询的配置，R5-3）。
// 文件读取与文本解析属宿主职责（框架不依赖文件系统，CS-§11）；本模块只解析 JSON 值。
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "npc_agent/interfaces/types.h"

namespace npc_agent::core {

// 感知注入配置：radius > 0 时 AgentSystem 每 tick 代执行 IWorld::sense 并写黑板。
struct PerceptionConfig {
    float radius = 0.0f;
    std::string sense_type = "sight";
};

struct AgentConfig {
    std::string id;                     // 必填，非空
    std::string decision_kind = "none"; // v1 记录用途（阶段 2 与决策器实现接线）
    uint64_t rng_seed = 0;
    PerceptionConfig perception;
    nlohmann::json extra = nlohmann::json::object(); // 保留扩展
};

// 配置校验错误（可定位，fail-fast 语义，与 config/feature_flags 同风格）。
struct ConfigError {
    std::string key_path;
    std::string expected;
    std::string actual;
};

// 解析并校验 NPC 配置 JSON。
// 【任意线程】纯函数。source 用于错误定位（通常为文件名）。
// 成功：out 填充，返回 nullopt；失败：返回 ConfigError，out 未定义。
// 复杂度：O(n)，n 为 JSON 文本长度。
std::optional<ConfigError> parse_agent_config(const nlohmann::json& in, std::string_view source,
                                              AgentConfig& out);

std::string to_string(const ConfigError& err);

} // namespace npc_agent::core
