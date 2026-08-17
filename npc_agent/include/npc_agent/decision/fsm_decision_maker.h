// FsmDecisionMaker —— 有限状态机决策器（npc_agent/decision/，阶段 2，RA 决策表 #18 v1 互斥单选）。
// 数据驱动：状态/迁移/意图全部由 JSON 定义（parse_fsm_definition，fail-fast 校验），
// 运行时仅保存"当前状态 + 进入时刻"（随存档序列化）。
// 语义（RA-§3.4）：propose 为权威意图源——当前状态有意图模板时返回 ready 意图；
// "idle" 状态返回 nullopt（不参与仲裁，模块候选可接管，与 pending 语义不同）。
// 迁移条件 v1 三类（AND 求值）：黑板键比对 bb / 状态停留时长 elapsed_ge / 兜底 default。
// 确定性契约（RA-§3.7）：条件求值无随机源，仅依赖黑板与 TickContext。
// 线程契约：全部方法【驱动线程】；parse_fsm_definition 为【任意线程】纯函数。
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::decision {

// 迁移：目标状态名 + 条件（JSON，见 parse_fsm_definition 校验）。
struct FsmTransition {
    std::string target;
    nlohmann::json condition = nlohmann::json::object();
};

// 状态定义：名称 / 意图模板（解析期编译为 Intent 缓存）/ 仲裁优先级 / 迁移表。
// 迁移按声明顺序评估，首个命中即迁移（条件互斥是定义方责任）。
struct FsmStateDef {
    std::string name;
    std::optional<Intent> intent; // nullopt = idle（无意图，让位模块候选）
    float priority = 1.0f;
    std::vector<FsmTransition> transitions;
};

// FSM 定义：初始状态名 + 状态表。
struct FsmDefinition {
    std::string initial;
    std::vector<FsmStateDef> states;
};

// 解析并校验 FSM 定义（fail-fast，CS-§9）：
//   { "initial": "idle",
//     "states": [ { "name": "idle", "priority": 1.0,
//                   "intent": {"kind":"idle"|"move_to"|"say"|"emote"|"game_event", ...},
//                   "transitions": [ {"target":"alert",
//                                     "condition":{"bb":{"alarm":true},"elapsed_ge":2.0,"default":true}}
//                                     ] } ] }
// 校验：initial 存在、状态名唯一且非空、迁移目标存在、intent 字段合法。
// 成功返回 nullopt；失败返回错误信息（含定位路径）。
std::optional<std::string> parse_fsm_definition(const nlohmann::json& in, FsmDefinition& out);

class FsmDecisionMaker final : public IDecisionMaker {
public:
    // 定义必须经 parse_fsm_definition 校验（未校验定义属编程错误，CS-§9）。
    explicit FsmDecisionMaker(FsmDefinition def);

    std::string_view id() const override { return "fsm"; }

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext& tc) override;

    void to_json(nlohmann::json& out) const override;  // 运行时状态（定义由构造注入）
    void from_json(const nlohmann::json& in) override; // 恢复当前状态；名称无效则保持未启动

    // 观察（trace/调试用）。
    std::string_view current_state() const;

private:
    bool evaluate(const nlohmann::json& condition, const core::Blackboard& bb,
                  const TickContext& tc) const;
    const FsmStateDef* find_state(std::string_view name) const;

    FsmDefinition def_;
    const FsmStateDef* current_ = nullptr;
    double entered_at_ = 0.0; // 进入当前状态的 game_time（elapsed_ge 求值基准）
    bool started_ = false;    // 首 tick 只记录进入时刻，次 tick 起评估迁移
};

} // namespace npc_agent::decision
