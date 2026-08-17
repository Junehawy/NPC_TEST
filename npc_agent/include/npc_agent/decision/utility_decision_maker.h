// UtilityDecisionMaker —— 效用计分决策器（npc_agent/decision/，阶段 2，RA 决策表 #18 v1
// 互斥单选）。 每个选项 = 条件（bb/default，共享求值）+ 意图模板 + 基础分 + 确定性噪声；
// propose：过滤条件不满足的选项 → 得分 = base_score + noise → 最高分胜出（同分取先声明）。
// 确定性契约（RA-§3.7 / R8）：噪声唯一来源为 tc.rng_seed 与选项名——FNV-1a（跨平台稳定）
// 混合后经 splitmix64 派生均匀值，禁 std::rand / 系统时钟；同种子同定义 = 同结果（trace 可复现）。
// v1 无运行时状态（纯函数式）；last_picked/last_score 仅供 trace/调试观察。
// 线程契约：全部方法【驱动线程】；parse_utility_definition 为【任意线程】纯函数。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::decision {

// 计分选项：条件缺省 = 恒真；intent 缺省/kind=idle = 待机（胜出即无意图）。
struct UtilityOption {
    std::string name;
    nlohmann::json condition = nlohmann::json::object();
    std::optional<Intent> intent;
    float base_score = 0.0f;
    float noise_amplitude = 0.0f; // 均匀噪声 ±amplitude（0 = 纯静态计分）
};

struct UtilityDefinition {
    std::vector<UtilityOption> options;
};

// 解析并校验（fail-fast）：
//   { "options": [ { "name": "patrol", "base_score": 1.0, "noise_amplitude": 0.2,
//                    "condition": {"bb": {"enemy_visible": false}},
//                    "intent": {"kind": "move_to", ...} } ] }
// 校验：options 数组存在、选项名唯一非空、条件键仅 bb/default、
// intent 模板合法、noise_amplitude ≥ 0。
std::optional<std::string> parse_utility_definition(const nlohmann::json& in,
                                                    UtilityDefinition& out);

class UtilityDecisionMaker final : public IDecisionMaker {
public:
    explicit UtilityDecisionMaker(UtilityDefinition def);

    std::string_view id() const override { return "utility"; }

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext& tc) override;

    void to_json(nlohmann::json& out) const override;  // 观察状态（非决策状态）
    void from_json(const nlohmann::json& in) override; // 尽力恢复观察字段

    // 观察（trace/调试用）。
    std::string_view last_picked() const;
    float last_score() const;

private:
    UtilityDefinition def_;
    std::string last_picked_; // 最近胜出选项名（空 = 无选项命中）
    float last_score_ = 0.0f;
};

} // namespace npc_agent::decision
