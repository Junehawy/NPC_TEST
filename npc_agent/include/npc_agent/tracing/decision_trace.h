// DecisionTrace —— 决策日志 v1（npc_agent/tracing/，阶段 2，RA 决策表 #22 / R8）。
// JSON lines 格式：每 tick 每 Agent 一行，含 tick_index / game_time / rng_seed /
// 决策来源 / 候选表 / 胜出意图 / 执行动作句柄（R8 定案字段）。
// 回放语义（R8）：同配置、同 rng_seed、同刺激脚本重跑 → compare() 逐行比对，
// 阈值 = 严格一致，首个分歧行即失败并给出定位。
// 线程契约：【驱动线程】（录制随 AgentSystem::tick）。
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "npc_agent/interfaces/intent.h"

namespace npc_agent::tracing {

// 意图 → JSON（trace 行内嵌）：{"kind":..., 字段}；nullopt → null。
nlohmann::json intent_to_json(const std::optional<Intent>& intent);

class DecisionTrace {
public:
    // 追加一行（每 tick 每 Agent）。
    void append(nlohmann::json line);

    const std::vector<nlohmann::json>& lines() const;
    std::size_t size() const;
    void clear();

    // 序列化为 JSON lines 文本（每行一个对象）。
    std::string dump() const;

    // 从 JSON lines 文本加载；失败返回 false 并给出 error（含行号）。
    static bool load(std::string_view text, DecisionTrace& out, std::string& error);

    // 逐行比对（R8：严格一致）。相同返回 nullopt；
    // 不同返回错误信息（含首个分歧行号与两侧摘要），first_diff 输出分歧下标。
    static std::optional<std::string> compare(const DecisionTrace& a, const DecisionTrace& b,
                                              std::size_t* first_diff = nullptr);

private:
    std::vector<nlohmann::json> lines_;
};

} // namespace npc_agent::tracing
