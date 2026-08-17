// BtDecisionMaker —— 行为树决策器（npc_agent/decision/，阶段 2，RA 决策表 #18 v1 互斥单选）。
// 数据驱动（JSON 定义，fail-fast 校验），v1 节点：selector / sequence / condition / action。
// 语义：
//   - selector：子节点按声明顺序回退，首个条件成立的子树生效（条件每 tick 重评，
//     变化即切换分支）；
//   - sequence：子节点依序推进——当前叶子的 done_when 满足则进入下一子节点，
//     末子节点完成后回到首个子节点循环（循环语义，v1）；中途子节点条件失败则整序重置到首子节点；
//   - condition：装饰器，条件成立才评估 child（bb/default，共享求值）；
//   - action：叶子，产出意图模板（kind=idle = 无意图，让位模块候选）；
//     done_when（可选，仅允许直接位于 sequence 下）：{"elapsed_ge":秒} 相位时长 或
//     {"bb":{...}} 黑板条件，满足即推进——支撑"警戒 → 呼叫支援 → 搜寻"式阶段序列。
// 读档语义（RA 决策表 #19）：to_json 不保存运行状态，from_json 重置到根节点重评。
// 确定性契约（RA-§3.7）：无随机源，仅依赖黑板与 TickContext。
// 线程契约：全部方法【驱动线程】；parse_bt_definition 为【任意线程】纯函数。
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "npc_agent/core/blackboard.h"
#include "npc_agent/interfaces/icapability.h"
#include "npc_agent/interfaces/intent.h"

namespace npc_agent::decision {

// 节点类型（v1）。
enum class BtNodeKind { Selector, Sequence, Condition, Action };

struct BtNode {
    BtNodeKind kind = BtNodeKind::Action;
    // action：意图模板（nullopt = idle）；condition：黑板条件；其他节点不使用。
    nlohmann::json condition = nlohmann::json::object();
    std::optional<Intent> intent;
    // action 专属：相位完成条件（可选；仅 sequence 直接子节点合法）。
    nlohmann::json done_when = nlohmann::json::object();
    bool has_done_when = false;
    // 结构子节点（selector/sequence/condition 使用；action 恒空）。
    std::vector<std::unique_ptr<BtNode>> children;
};

// 解析并校验 BT 定义（fail-fast）：
//   { "root": { "kind": "sequence", "children": [
//       { "kind": "action", "intent": {"kind":"emote","name":"startled"},
//         "done_when": {"elapsed_ge": 1.0} },
//       { "kind": "action", "intent": {"kind":"say","text":"呼叫支援"},
//         "done_when": {"bb": {"support_called": true}} },
//       { "kind": "action", "intent": {"kind":"move_to","target":[0,0,0]} } ] } }
// 校验：kind 合法、action 必须有 intent、selector/sequence 子节点非空、
// condition 必须有 child、done_when 仅允许直接位于 sequence 下且键仅 bb/elapsed_ge。
std::optional<std::string> parse_bt_definition(const nlohmann::json& in, BtNode& out_root);

class BtDecisionMaker final : public IDecisionMaker {
public:
    explicit BtDecisionMaker(BtNode root);

    std::string_view id() const override { return "bt"; }

    std::optional<Intent> propose(const core::Blackboard& bb, const TickContext& tc) override;

    void to_json(nlohmann::json& out) const override;  // v1：无运行状态（#19）
    void from_json(const nlohmann::json& in) override; // 重置到根节点重评（#19）

    // 观察（trace/调试用）：当前活跃叶子的调试路径（如 root/0/1）。
    std::string current_path() const;

private:
    struct PathEntry {
        const BtNode* node = nullptr;
        std::size_t child_index = 0; // 所在 selector/sequence 的下标（根节点无意义）
    };

    // 条件选择遍历：按 selector/sequence/condition 语义找到活跃叶子路径。
    // 返回 false = 整树无条件可运行（无意图）。
    bool select_path(const BtNode& node, std::vector<PathEntry>& path, const core::Blackboard& bb,
                     const TickContext& tc) const;
    // done_when 推进：当前叶已满足完成条件时沿 sequence 前进（循环；会更新相位计时）。
    void advance_done(std::vector<PathEntry>& path, const core::Blackboard& bb,
                      const TickContext& tc);
    bool done_when_satisfied(const BtNode& leaf, const core::Blackboard& bb,
                             const TickContext& tc) const;
    void reset_runtime(const TickContext& tc);

    BtNode root_;
    std::vector<PathEntry> path_;   // 当前活跃路径（根 → 叶）
    double phase_started_at_ = 0.0; // 当前相位起始 game_time（elapsed_ge 基准）
    bool started_ = false;
};

} // namespace npc_agent::decision
