// 三级开关体系：编译期 × 运行期（超集约束校验，fail-fast）。
// 契约来源：RA-§5.2 —— 编译期开关是运行期开关的超集约束：
// 编译期关闭的能力，运行期配置中出现 enabled=true 必须报错，绝不静默忽略。
// v1 最小 schema（随能力模块交付扩展，RA-§7.3）：
//   { "capabilities": {
//       "llm":           { "enabled": bool },   // LLM 能力（阶段 5 追加 provider 等字段）
//       "memory_vector": bool                   // 记忆向量检索（bool 简写）
//   } }
#pragma once

// 兜底默认值：构建系统未注入时视为关闭（供开关矩阵二进制直接重编本模块）。
#ifndef NPC_AGENT_ENABLE_LLM
#define NPC_AGENT_ENABLE_LLM 0
#endif
#ifndef NPC_AGENT_ENABLE_MEMORY_VECTOR
#define NPC_AGENT_ENABLE_MEMORY_VECTOR 0
#endif

#include <optional>
#include <string>
#include <string_view>

namespace npc_agent::config {

// 编译期能力开关：由 CMake 选项经编译定义注入。
// 纯静态常量，无生命周期约束；只读，全值语义。
struct CompileFlags {
    static constexpr bool kLlmCompiled = (NPC_AGENT_ENABLE_LLM != 0);
    static constexpr bool kMemoryVectorCompiled = (NPC_AGENT_ENABLE_MEMORY_VECTOR != 0);
};

// 运行期能力开关（解析后的可查询结果）。
// 默认全部关闭，与 RA-§5.1 能力清单"默认关"一致。
struct RuntimeFlags {
    bool llm_enabled = false;
    bool memory_vector_enabled = false;
};

// 配置校验错误：一条可定位的失败信息（fail-fast 语义）。
// file / key_path 定位出错位置；expected / actual 说明差异。
struct SwitchError {
    std::string file;
    std::string key_path;
    std::string expected;
    std::string actual;
};

// 解析并校验能力开关 JSON。
// 【任意线程】纯函数：无副作用、不持有任何指针，可在任何线程调用。
// 前置：json_text 为 UTF-8 文本；source_name 用于错误定位（通常为配置文件路径）。
// 成功：out 填充 RuntimeFlags，返回 std::nullopt；失败：返回 SwitchError，out 未定义。
// 复杂度：O(n)（nlohmann 单次解析 + 常数个键遍历），n 为文本长度。
std::optional<SwitchError> parse_and_validate(std::string_view json_text,
                                              std::string_view source_name, RuntimeFlags& out);

// 将 SwitchError 格式化为一行可读信息（日志/测试输出用）。
// 返回字符串长度 O(|file| + |key_path| + |expected| + |actual|)。
std::string to_string(const SwitchError& err);

} // namespace npc_agent::config
