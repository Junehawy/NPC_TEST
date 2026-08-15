// ILLMProvider —— LLM 抽象（RA-§3.6）：对话=纯文本，决策=JSON schema。
// 异步铁律：submit 永不阻塞；回调经线程安全待处理队列在驱动线程派发，
// 回调内可直接读写 Blackboard（RA-§2.2）。
// 取消语义分层：取消回调由 AgentSystem 令牌作废负责；cancel() 尽力中断底层
// 请求（默认空操作），是否真正中断取决于 Provider 实现（RA-§9 #26）。
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "npc_agent/interfaces/types.h"

namespace npc_agent {

struct Message {
    std::string role; // "system" / "user" / "assistant"
    std::string content;
};

enum class OutputMode { Text, JsonSchema };

struct LLMRequest {
    std::string system_prompt; // 角色卡渲染结果（PromptBuilder 产出）
    std::vector<Message> history;
    OutputMode mode = OutputMode::Text;
    json schema = json::object(); // mode==JsonSchema 时提供（function calling）
};

struct LLMResponse {
    bool ok = false;
    std::string text;           // Text 模式
    json json = json::object(); // JsonSchema 模式
};

struct ILLMProvider {
    virtual ~ILLMProvider() = default;

    // 提交请求，结果经回调在驱动线程派发；永不阻塞调用线程。
    // 回调持有所有权：实现必须保证在请求完成或取消前保持其有效。
    virtual AsyncToken submit(LLMRequest req, std::function<void(LLMResponse)> cb) = 0;

    // 尽力中断底层请求（省钱/省资源）；默认空操作。
    virtual void cancel(AsyncToken /*token*/) {}
};

} // namespace npc_agent
