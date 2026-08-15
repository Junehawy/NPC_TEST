#include "npc_agent/core/agent_config.h"

#include <string>

namespace npc_agent::core {

namespace {

[[nodiscard]] ConfigError make_error(std::string_view key_path, std::string_view expected,
                                     std::string_view actual) {
    return ConfigError{std::string(key_path), std::string(expected), std::string(actual)};
}

// 校验感知配置子对象（radius ≥ 0 的数值 + sense_type 字符串）。
[[nodiscard]] std::optional<ConfigError> parse_perception(const nlohmann::json& node,
                                                          PerceptionConfig& out) {
    if (!node.is_object())
        return make_error("perception", "JSON 对象", node.type_name());
    if (node.contains("radius")) {
        if (!node["radius"].is_number() || node["radius"].get<float>() < 0.0f) {
            return make_error("perception.radius", "非负数", node["radius"].dump());
        }
        out.radius = node["radius"].get<float>();
    }
    if (node.contains("sense_type")) {
        if (!node["sense_type"].is_string()) {
            return make_error("perception.sense_type", "字符串", node["sense_type"].dump());
        }
        out.sense_type = node["sense_type"].get<std::string>();
    }
    return std::nullopt;
}

} // namespace

std::optional<ConfigError> parse_agent_config(const nlohmann::json& in, std::string_view source,
                                              AgentConfig& out) {
    if (!in.is_object()) {
        return make_error("<root>", "JSON 对象", in.type_name());
    }

    AgentConfig cfg;
    for (const auto& [key, value] : in.items()) {
        if (key == "id") {
            if (!value.is_string() || value.get<std::string>().empty()) {
                return make_error("id", "非空字符串", value.dump());
            }
            cfg.id = value.get<std::string>();
        } else if (key == "decision") {
            if (!value.is_string())
                return make_error("decision", "字符串", value.dump());
            cfg.decision_kind = value.get<std::string>();
        } else if (key == "rng_seed") {
            // 注：nlohmann 将正整数解析为有符号 number_integer，
            // 需同时接受有符号非负与无符号两种表示。
            const bool ok = value.is_number_unsigned() ||
                            (value.is_number_integer() && value.get<int64_t>() >= 0);
            if (!ok)
                return make_error("rng_seed", "非负整数", value.dump());
            cfg.rng_seed = value.is_number_unsigned() ? value.get<uint64_t>()
                                                      : static_cast<uint64_t>(value.get<int64_t>());
        } else if (key == "perception") {
            if (auto err = parse_perception(value, cfg.perception); err)
                return err;
        } else if (key == "extra") {
            cfg.extra = value; // 保留字段，任意 JSON
        } else {
            return make_error(key, "已知键（id / decision / rng_seed / perception / extra）",
                              "未知键 '" + key + "'");
        }
    }

    if (cfg.id.empty()) {
        return make_error("id", "非空字符串（必填）", "缺失");
    }

    out = std::move(cfg);
    (void)source; // v1 错误不含文件名（ConfigError 无 file 字段），保留参数以备扩展
    return std::nullopt;
}

std::string to_string(const ConfigError& err) {
    return "NPC 配置错误: " + err.key_path + " —— 期望 " + err.expected + "，实际 " + err.actual;
}

} // namespace npc_agent::core
