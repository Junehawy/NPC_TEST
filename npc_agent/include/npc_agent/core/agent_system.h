// AgentSystem —— 全局驱动（RA-§2.2）：统一 tick 所有 Agent、持有当前场景
// IWorld 引用（框架内唯一持有者，感知/导航查询由其代执行并注入结果）、
// 全局事件队列、组装 AgentSnapshot、管理存档。
// 线程契约：全部成员函数【驱动线程】调用。
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "npc_agent/core/agent.h"
#include "npc_agent/core/capability_factory.h"
#include "npc_agent/interfaces/i_world.h"

namespace npc_agent::core {

class AgentSystem {
public:
    AgentSystem() = default;

    // 当前场景引用（顺序换图/传送时更新；并发多场景为 v1 排除范围，RA-§13 #11）。
    void set_current_world(IWorld& world);
    IWorld* current_world() const;

    // 存档恢复用能力工厂（调用 restore 前必须设置）。
    void set_capability_factory(const CapabilityFactory* factory);

    // 驱动所有 Agent 一个 tick：感知注入（R5-3）→ 快照组装 → Agent::tick → 执行胜出意图。
    // 无当前场景时为空操作。
    void tick();

    // 宿主推送瞬时刺激：转发 IWorld 记录 + 转全局事件广播（RA-§3.2）。
    void inject_stimulus(const Stimulus& s);

    // 全局事件广播（下个 tick 派发给所有 Agent 的能力）。
    void broadcast(AgentEvent e);

    // 创建 Agent 并挂身体（装配期使用；能力经 register_capability 注册）。
    Agent& create_agent(AgentConfig config, IAgentBody& body);
    Agent* find_agent(std::string_view id);
    std::size_t agent_count() const;
    const std::vector<std::unique_ptr<Agent>>& agents() const;

    nlohmann::json to_json() const;
    // 恢复：需先 set_capability_factory；world 与各 Agent 身体由调用方重挂
    // （set_current_world + find_agent()->attach_body）。失败返回 false 并给出 error。
    static bool restore(const nlohmann::json& in, AgentSystem& out, std::string& error);

private:
    IWorld* world_ = nullptr;
    const CapabilityFactory* factory_ = nullptr;
    std::deque<AgentEvent> global_queue_;
    std::vector<std::unique_ptr<Agent>> agents_;
    std::vector<AgentEvent> global_scratch_; // tick 内复用（CS-§7.5）
};

} // namespace npc_agent::core
