// 跨边界 POD 契约类型（RA-§3.1）。
// 规则：接口层只出现自研 POD/JSON 值语义类型，不含引擎/第三方类型（CS-§1.3）。
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "npc_agent/interfaces/memory_event.h"

namespace npc_agent {

// 便捷别名（仅本命名空间内使用；头文件不写 using namespace）。
using json = nlohmann::json;

// 三维向量：纯数据、无运算（运算逻辑留在实现内部，RA-§3.1）。
struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

// 单次 tick 的上下文：所有 tick 函数入参（RA-§4）。
// rng_seed 由 Agent 根 RNG 每 tick 确定性派生（RA-§3.7），不来自宿主。
struct TickContext {
    float dt = 0.0f;         // 本次 tick 模拟步长（秒）
    double game_time = 0.0;  // 全局游戏时间（秒）；暂停不推进、同运行内单调
    uint64_t tick_index = 0; // 单调递增 tick 序号
    uint64_t rng_seed = 0;   // 本 tick 确定性随机种子（决策随机唯一来源）
};

// 环境快照（值语义、可序列化）：只含环境，不含 self（RA-§3.1）。
struct WorldSnapshot {
    double game_time = 0.0;
    std::vector<MemoryEvent> recent_events;
    json extra = json::object(); // 其余环境只读字段（天气、可见实体等）
};

// 自身状态快照：由 IAgentBody 提供（RA-§3.1）。
struct BodyState {
    Vec3 position;
    std::string faction;
    float stamina = 1.0f;
    json extra = json::object(); // 其余自身只读字段
};

// 完整 Agent 快照：AgentSystem 在驱动线程组装（WorldSnapshot + BodyState）。
// 工作线程只接触它，绝不接触接口对象（RA-§2.1 线程契约）。
struct AgentSnapshot {
    WorldSnapshot world;
    BodyState self;
};

// 动作生命周期句柄（RA-§3.1）：完成/失败经事件总线回投。
struct ActionHandle {
    uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

// 轻量异步原语（RA-§3.6）：回调经事件队列在驱动线程派发，不提供阻塞 get。
struct AsyncToken {
    uint64_t id = 0;
};

} // namespace npc_agent
