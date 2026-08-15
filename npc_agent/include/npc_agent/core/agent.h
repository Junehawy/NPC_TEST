// Agent —— 单 NPC 运行时对象（RA-§2.2 per-agent）。
// 持有：能力模块（均实现 ICapability）、决策器（权威意图源）、私有事件队列、
// 自己的 Blackboard、仲裁器、根 RNG（RA-§3.7 唯一随机源）。
// 线程契约：全部成员函数【驱动线程】调用。
#pragma once

#include <deque>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "npc_agent/core/agent_config.h"
#include "npc_agent/core/blackboard.h"
#include "npc_agent/core/capability_factory.h"
#include "npc_agent/core/event.h"
#include "npc_agent/interfaces/i_agent_body.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"
#include "npc_agent/interfaces/types.h"

namespace npc_agent::core {

class Agent {
public:
    explicit Agent(AgentConfig config);

    // ---- 身份与身体 ----
    std::string_view id() const;
    const AgentConfig& config() const;
    // 装配或读档恢复后重挂身体（CS-§9：未挂身体即访问状态为编程错误）。
    void attach_body(IAgentBody& body);
    BodyState body_state() const; // 接口按值返回，此处透传

    // ---- 能力注册：注册序即仲裁同分次序（RA-§3.5 框架分配，模块不自报） ----
    void register_capability(std::unique_ptr<ICapability> cap);
    void set_decision_maker(std::unique_ptr<IDecisionMaker> dm);

    // ---- 每 tick 主流程（RA-§3.4 仲裁管线） ----
    // 1) 排空私有事件并派发；2) 派发全局事件；3) 根 RNG 确定性推进派生 rng_seed；
    // 4) 决策器 ready 意图直接胜出；5) 其余候选 priority 降序、同分按注册序。
    // 返回仲裁胜出意图并缓存于 last_intent()；无候选返回 nullopt。
    std::optional<Intent> tick(const TickContext& tc, std::span<const AgentEvent> global_events);

    // 私有事件入队（下个 tick 派发；动作完成回投等）。
    void enqueue_private(AgentEvent e);

    // 执行意图（经 IAgentBody）。ready=false 的意图不可执行（调用方保证）。
    void execute(const Intent& intent);

    // ---- 观察与存档 ----
    const std::optional<Intent>& last_intent() const;
    Blackboard& blackboard();
    const Blackboard& blackboard() const;
    const AgentSnapshot& last_snapshot() const;
    void update_snapshot(AgentSnapshot snap);

    void to_json(nlohmann::json& out) const;
    // 恢复：能力经 CapabilityFactory 重建；身体由调用方 attach_body() 后挂。
    // 失败：error 给出定位信息，返回 nullopt。
    static std::optional<Agent> restore(const nlohmann::json& in, const CapabilityFactory& factory,
                                        std::string& error);

private:
    // 仲裁候选缓冲（复用，避免每 tick 分配，CS-§7.5）。
    struct Candidate {
        const ICapability* cap;
        Intent intent;
    };

    void route_event(const AgentEvent& e);

    AgentConfig config_;
    IAgentBody* body_ = nullptr;
    std::vector<std::unique_ptr<ICapability>> caps_; // 注册序 = 仲裁同分次序
    std::unique_ptr<IDecisionMaker> decision_maker_; // 权威意图源
    Blackboard bb_;
    std::deque<AgentEvent> private_queue_;
    std::mt19937_64 rng_;
    std::optional<Intent> last_intent_;
    AgentSnapshot last_snapshot_;
    std::vector<Candidate> scratch_; // 候选缓冲（随注册增长，tick 内复用）
};

} // namespace npc_agent::core
