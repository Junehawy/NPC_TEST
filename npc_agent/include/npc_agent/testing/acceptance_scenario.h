// AcceptanceScenario —— 阶段 2 验收场景（npc_agent/testing/，RA 路线图 2.x 验收行）：
// "听到枪声 → 警戒 → 呼叫支援 → 搜寻"，由 FSM guard + 感知模块在 MockWorld 上跑通。
// 确定性（R8）：固定 rng_seed、固定 dt、刺激按 tick 序号对齐（tick 3 注入枪声）——
// 同一场景重复运行产出逐字节一致的决策日志，供 trace 录制/回放回归（CLI 与 ctest 共用）。
// 线程契约：【驱动线程】。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "npc_agent/core/agent_system.h"
#include "npc_agent/testing/mock_body.h"
#include "npc_agent/testing/mock_world.h"

namespace npc_agent::testing {

class AcceptanceScenario {
public:
    // seed 变化仅影响 trace 的 rng_seed 字段（本场景无随机决策），供回放负向断言。
    explicit AcceptanceScenario(uint64_t seed = 42);

    // 推进 ticks 个 tick（每 tick：脚本刺激注入 → advance(dt) → system.tick()）。
    void run(std::size_t ticks, double dt = 0.1);

    core::AgentSystem& system();
    core::Agent& agent();
    MockWorld& world();

    // FSM 定义（record/replay/文档共用）。
    static std::string fsm_definition_json();

    static constexpr std::string_view kAgentId = "guard";
    static constexpr uint64_t kGunshotTick = 3;      // 枪声注入 tick（确定性脚本）
    static constexpr uint64_t kAlertTick = 3;        // FSM 进入警戒（同 tick 感知生效）
    static constexpr uint64_t kCallSupportTick = 13; // 警戒 1s（dt=0.1）后呼叫支援
    static constexpr uint64_t kSearchTick = 28;      // 呼叫支援 1.5s 后开始搜寻

private:
    MockWorld world_;
    MockBody body_;
    core::AgentSystem system_;
    core::Agent* agent_ = nullptr;
};

} // namespace npc_agent::testing
