// Intent —— 决策输出（RA-§3.4）：variant 负载 + 仲裁元数据。
// 仲裁管线（§3.4 四步）由 Agent 实现：决策器权威意图 → 候选按 priority 降序、
// 同分按注册序（框架分配，模块不自报）。
#pragma once

#include <optional>
#include <string>
#include <variant>

#include "npc_agent/interfaces/i_agent_body.h"
#include "npc_agent/interfaces/types.h"

namespace npc_agent {

struct MoveIntent {
    Vec3 target;
    float speed = 1.0f;
};

struct SayIntent {
    std::string text;
    std::string tone = "neutral";
};

struct EmoteIntent {
    std::string name;
};

struct GameEventIntent {
    GameEvent event;
};

using IntentPayload = std::variant<MoveIntent, SayIntent, EmoteIntent, GameEventIntent>;

struct Intent {
    IntentPayload payload;
    float priority = 0.0f;
    // false = 决策未完成，正在等异步结果（LLM）；执行层用兜底行为顶替（RA-§3.4）。
    bool ready = true;
    std::optional<AsyncToken> async_token; // ready=false 时的关联令牌
};

} // namespace npc_agent
