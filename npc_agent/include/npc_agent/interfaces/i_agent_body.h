// IAgentBody —— 智能体的"身体"接口（RA-§3.3）：每 NPC 一个。
// 自身状态（只读）+ 动作执行（返回句柄，完成/失败经事件回投）。
// 线程契约：全部方法【驱动线程】调用；能力模块不得持有本接口指针（Blackboard-only 契约）。
#pragma once

#include <string>

#include "npc_agent/interfaces/types.h"

namespace npc_agent {

// 一句台词：纯数据，由宿主播放。
struct DialogueLine {
    std::string text;
    std::string tone = "neutral";
};

// 通用语义通道负载（领域动作：交任务/给物品/开锁…，宿主自解释，RA-§3.3）。
struct GameEvent {
    std::string type;
    json payload = json::object();
};

struct IAgentBody {
    virtual ~IAgentBody() = default;

    // 只读：自身状态（供 AgentSystem 组装 AgentSnapshot）。
    virtual BodyState body_state() const = 0;

    // 动作：返回 ActionHandle；完成/失败/取消经事件总线回投（RA-§3.3）。
    // 移动是持续动作，执行层必须能收到"到达 / 被挡住 / 被打断"。
    virtual ActionHandle move_to(Vec3 target, float speed) = 0;
    virtual ActionHandle play_emote(const std::string& name) = 0;
    virtual ActionHandle say(const DialogueLine& line) = 0;

    // 领域动作通道：返回句柄，生命周期与其余动作对称。
    // 宿主不回报完成时框架按"立即成功"推进；任务进度以条件求值为准（RA-§9 #6）。
    virtual ActionHandle dispatch_game_event(const GameEvent& e) = 0;
};

} // namespace npc_agent
