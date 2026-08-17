// ICapability / IDecisionMaker —— 能力模块统一产出接口（RA-§3.5）。
// 注：本头文件依赖 core/Blackboard 与 core/AgentEvent，均为框架内部依赖
// （同一库内，不违反"接口层无第三方类型"红线）。
// 契约要点：
//  - 仲裁次序不由模块自报：由 Agent::register_capability() 按注册顺序分配；
//  - Blackboard-only：模块不得持有 IWorld/IAgentBody 指针；
//  - propose() 只从 Blackboard + TickContext 读取（RA-§3.5 硬性规则）；
//  - on_tick() 为每 tick 处理钩子（阶段 2 新增，R9）：事件派发后、仲裁前调用，
//    模块可写 Blackboard（感知打包/条件旗标等），即使决策器权威也会执行。
#pragma once

#include <optional>
#include <string_view>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/core/event.h"
#include "npc_agent/interfaces/intent.h"
#include "npc_agent/interfaces/serializable.h"
#include "npc_agent/interfaces/types.h"

namespace npc_agent {

struct ICapability : public ISerializable {
    ~ICapability() override = default;

    // 注册名（日志/诊断/存档工厂键）。
    virtual std::string_view id() const = 0;

    // 产出候选意图。返回值语义：
    //  - nullopt：本 tick 无候选；
    //  - ready=true：参与仲裁；
    //  - ready=false：等待异步结果，不参与仲裁（RA-§3.4）。
    // 【驱动线程】；实现不得缓存/持有 IWorld 与 IAgentBody 指针。
    virtual std::optional<Intent> propose(const core::Blackboard& bb, const TickContext& tc) = 0;

    // 事件通知（全局广播 + 本 Agent 私有事件），【驱动线程】派发（RA-§2.2）。
    // 默认忽略；注意不得在回调内再入驱动循环（CS-§8.4）。
    virtual void on_event(const core::AgentEvent& /*e*/) {}

    // 每 tick 处理钩子（阶段 2 新增，R9）：事件派发后、仲裁前由 Agent 调用，
    // 先于决策器 propose。模块可写 Blackboard（如感知打包、条件旗标），
    // 决策器权威期间同样执行——保证感知状态持续刷新。
    // 【驱动线程】；确定性要求同 propose（禁系统随机源，RA-§3.7）。
    // 默认空操作：既有模块无需改动。
    virtual void on_tick(core::Blackboard& /*bb*/, const TickContext& /*tc*/) {}

    // to_json/from_json 继承自 ISerializable（模块私有状态随存档，RA-§7.3）。
};

// 决策器：纯标记子接口（RA-§3.5）。其 ready 意图在仲裁中拥有权威地位，
// 权威地位由 Agent 在注册/配置时指定；只统一输入输出契约，不强制内部机制。
struct IDecisionMaker : ICapability {};

} // namespace npc_agent
