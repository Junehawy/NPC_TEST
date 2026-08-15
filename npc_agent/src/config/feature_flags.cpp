// 实现：JSON 解析 + fail-fast 校验（RA-§5.2 超集约束）。
#include "npc_agent/config/feature_flags.h"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

namespace npc_agent::config {

namespace {

using nlohmann::json;

// v1 最小 schema 允许的键（新增能力随模块交付扩展，RA-§7.3）。
constexpr std::string_view kCapabilities = "capabilities";
constexpr std::string_view kKeyLlm = "llm";
constexpr std::string_view kKeyMemoryVector = "memory_vector";

[[nodiscard]] SwitchError make_error(std::string_view file, std::string_view key_path,
                                     std::string_view expected, std::string_view actual) {
    return SwitchError{std::string(file), std::string(key_path), std::string(expected),
                       std::string(actual)};
}

// 校验"对象 + enabled: bool"形式的能力开关（llm）。
// 含超集约束：编译期未包含却请求启用 → fail-fast 报错。
// 复杂度：O(1)。
[[nodiscard]] std::optional<SwitchError> check_object_switch(const json& node,
                                                            std::string_view key, bool compiled,
                                                            std::string_view compile_macro,
                                                            std::string_view file,
                                                            bool& out_enabled) {
    const std::string path = std::string(kCapabilities) + "." + std::string(key);
    if (!node.is_object()) {
        return make_error(file, path, "对象（{\"enabled\": bool}）", node.type_name());
    }
    if (!node.contains("enabled")) {
        return make_error(file, path + ".enabled", "布尔值", "缺失");
    }
    if (!node["enabled"].is_boolean()) {
        return make_error(file, path + ".enabled", "布尔值", node["enabled"].type_name());
    }
    const bool enabled = node["enabled"].get<bool>();
    if (enabled && !compiled) {
        return make_error(file, path + ".enabled",
                          std::string("该能力未在编译期包含（") + std::string(compile_macro) +
                              "=OFF），运行期不可启用",
                          "true");
    }
    out_enabled = enabled;
    return std::nullopt;
}

// 校验 bool 简写形式的能力开关（memory_vector）。语义同上。复杂度：O(1)。
[[nodiscard]] std::optional<SwitchError> check_bool_switch(const json& node,
                                                           std::string_view key, bool compiled,
                                                           std::string_view compile_macro,
                                                           std::string_view file,
                                                           bool& out_enabled) {
    const std::string path = std::string(kCapabilities) + "." + std::string(key);
    if (!node.is_boolean()) {
        return make_error(file, path, "布尔值", node.type_name());
    }
    const bool enabled = node.get<bool>();
    if (enabled && !compiled) {
        return make_error(file, path,
                          std::string("该能力未在编译期包含（") + std::string(compile_macro) +
                              "=OFF），运行期不可启用",
                          "true");
    }
    out_enabled = enabled;
    return std::nullopt;
}

}  // namespace

std::optional<SwitchError> parse_and_validate(std::string_view json_text,
                                              std::string_view source_name,
                                              RuntimeFlags& out) {
    json root;
    try {
        root = json::parse(json_text);
    } catch (const json::parse_error& e) {
        std::ostringstream actual;
        actual << "解析失败，字节偏移 " << e.byte;
        return make_error(source_name, "<json>", "合法 JSON 文本", actual.str());
    }
    if (!root.is_object()) {
        return make_error(source_name, "<root>", "JSON 对象", root.type_name());
    }

    RuntimeFlags flags;
    for (const auto& [key, value] : root.items()) {
        if (key == kCapabilities) {
            if (!value.is_object()) {
                return make_error(source_name, key, "JSON 对象", value.type_name());
            }
            for (const auto& [cap_key, cap_value] : value.items()) {
                if (cap_key == kKeyLlm) {
                    if (auto err = check_object_switch(cap_value, kKeyLlm,
                                                       CompileFlags::kLlmCompiled,
                                                       "NPC_AGENT_ENABLE_LLM", source_name,
                                                       flags.llm_enabled);
                        err) {
                        return err;
                    }
                } else if (cap_key == kKeyMemoryVector) {
                    if (auto err = check_bool_switch(cap_value, kKeyMemoryVector,
                                                     CompileFlags::kMemoryVectorCompiled,
                                                     "NPC_AGENT_ENABLE_MEMORY_VECTOR",
                                                     source_name, flags.memory_vector_enabled);
                        err) {
                        return err;
                    }
                } else {
                    return make_error(source_name,
                                      std::string(kCapabilities) + "." + std::string(cap_key),
                                      "已知能力键（llm / memory_vector）",
                                      "未知键 '" + cap_key + "'");
                }
            }
        } else {
            return make_error(source_name, key, "已知顶层键（capabilities）",
                              "未知键 '" + key + "'");
        }
    }

    out = flags;
    return std::nullopt;
}

std::string to_string(const SwitchError& err) {
    return "配置错误[" + err.file + "]: " + err.key_path + " —— 期望 " + err.expected +
           "，实际 " + err.actual;
}

}  // namespace npc_agent::config
