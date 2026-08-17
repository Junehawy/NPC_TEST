#include "npc_agent/decision/bt_decision_maker.h"

#include <utility>

#include "condition.h"
#include "intent_spec.h"

namespace npc_agent::decision {

namespace {

std::optional<std::string> parse_node(const json& in, BtNode& out, BtNodeKind parent_kind,
                                      const std::string& where) {
    if (!in.is_object())
        return where + " 应为对象";
    const auto kind_it = in.find("kind");
    if (kind_it == in.end() || !kind_it->is_string())
        return where + ".kind 应为字符串";
    const std::string kind = kind_it->get<std::string>();

    if (kind == "action") {
        out.kind = BtNodeKind::Action;
        const auto intent_it = in.find("intent");
        if (intent_it == in.end())
            return where + ".intent 缺失（action 必须有意图模板）";
        if (auto err = internal::parse_intent_spec(*intent_it, out.intent); err.has_value())
            return where + "." + *err;
        if (auto it = in.find("done_when"); it != in.end()) {
            if (parent_kind != BtNodeKind::Sequence)
                return where + ".done_when 仅允许位于 sequence 直接子节点";
            if (!it->is_object())
                return where + ".done_when 应为对象";
            if (!internal::condition_keys_allowed(*it, {"bb", "elapsed_ge"}))
                return where + ".done_when 含未知键（允许 bb/elapsed_ge）";
            out.done_when = *it;
            out.has_done_when = true;
        }
        return std::nullopt;
    }
    if (kind == "condition") {
        out.kind = BtNodeKind::Condition;
        const auto cond_it = in.find("condition");
        if (cond_it == in.end() || !cond_it->is_object())
            return where + ".condition 应为对象";
        if (!internal::condition_keys_allowed(*cond_it, {"bb", "default"}))
            return where + ".condition 含未知键（允许 bb/default）";
        out.condition = *cond_it;
        const auto child_it = in.find("child");
        if (child_it == in.end())
            return where + ".child 缺失（condition 必须有子节点）";
        auto child = std::make_unique<BtNode>();
        if (auto err = parse_node(*child_it, *child, BtNodeKind::Condition, where + ".child");
            err.has_value())
            return err;
        out.children.push_back(std::move(child));
        return std::nullopt;
    }
    if (kind == "selector" || kind == "sequence") {
        out.kind = kind == "selector" ? BtNodeKind::Selector : BtNodeKind::Sequence;
        const auto children_it = in.find("children");
        if (children_it == in.end() || !children_it->is_array() || children_it->empty())
            return where + ".children 应为非空数组";
        for (std::size_t i = 0; i < children_it->size(); ++i) {
            auto child = std::make_unique<BtNode>();
            const std::string cwhere = where + ".children[" + std::to_string(i) + "]";
            if (auto err = parse_node((*children_it)[i], *child, out.kind, cwhere); err.has_value())
                return err;
            out.children.push_back(std::move(child));
        }
        return std::nullopt;
    }
    return where + ".kind 未知: " + kind;
}

} // namespace

std::optional<std::string> parse_bt_definition(const json& in, BtNode& out_root) {
    if (!in.is_object())
        return std::string("BT 定义应为对象");
    const auto root_it = in.find("root");
    if (root_it == in.end())
        return std::string("BT 定义缺少 root 节点");
    BtNode root;
    if (auto err = parse_node(*root_it, root, BtNodeKind::Selector, "root"); err.has_value())
        return err;
    out_root = std::move(root);
    return std::nullopt;
}

BtDecisionMaker::BtDecisionMaker(BtNode root) : root_(std::move(root)) {
}

std::optional<Intent> BtDecisionMaker::propose(const core::Blackboard& bb, const TickContext& tc) {
    if (!started_) {
        path_.clear();
        if (!select_path(root_, path_, bb, tc))
            return std::nullopt; // 首 tick 即无条件可运行
        phase_started_at_ = tc.game_time;
        started_ = true;
    } else {
        // 条件重评：selector 每 tick 重选分支；sequence 维持当前下标（其子节点条件仍重验）。
        std::vector<PathEntry> fresh;
        if (!select_path(root_, fresh, bb, tc)) {
            reset_runtime(tc); // 无条件可运行：清运行状态，无意图
            return std::nullopt;
        }
        const bool same_leaf =
            !fresh.empty() && !path_.empty() && fresh.back().node == path_.back().node;
        path_ = std::move(fresh);
        if (!same_leaf)
            phase_started_at_ = tc.game_time; // 叶子切换：相位计时重置
        // done_when 推进（sequence 循环；见 advance_done）
        advance_done(path_, bb, tc);
    }
    const BtNode* leaf = path_.back().node;
    if (leaf->intent.has_value())
        return *leaf->intent; // 模板拷贝（阶段 6 池化目标，CS-§7.5 备忘）
    return std::nullopt;      // idle 叶子：无权威意图，模块候选接管
}

void BtDecisionMaker::to_json(json& out) const {
    out = json::object(); // v1：不保存运行状态（RA 决策表 #19）
}

void BtDecisionMaker::from_json(const json&) {
    // 读档重置到根节点重评（RA 决策表 #19）：清空运行状态，下个 tick 从根重新选择。
    path_.clear();
    started_ = false;
}

std::string BtDecisionMaker::current_path() const {
    std::string out = "root";
    // 末位为叶子节点，不计入结构路径（结构路径 = 各 selector/sequence 的下标）。
    for (std::size_t i = 0; i + 1 < path_.size(); ++i)
        out += "/" + std::to_string(path_[i].child_index);
    return out;
}

bool BtDecisionMaker::select_path(const BtNode& node, std::vector<PathEntry>& path,
                                  const core::Blackboard& bb, const TickContext& tc) const {
    switch (node.kind) {
    case BtNodeKind::Action: {
        path.push_back(PathEntry{&node, 0});
        return true;
    }
    case BtNodeKind::Condition: {
        if (!internal::evaluate_conditions(node.condition, bb))
            return false;
        path.push_back(PathEntry{&node, 0});
        return select_path(*node.children[0], path, bb, tc);
    }
    case BtNodeKind::Selector: {
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const std::size_t size_before = path.size();
            path.push_back(PathEntry{&node, i});
            if (select_path(*node.children[i], path, bb, tc))
                return true;
            path.resize(size_before); // 该分支不可运行：回退
        }
        return false;
    }
    case BtNodeKind::Sequence: {
        // 维持当前下标（运行期）；首 tick 从 0 开始。
        std::size_t index = 0;
        for (const auto& entry : path_) {
            if (entry.node == &node) {
                index = entry.child_index;
                break;
            }
        }
        const std::size_t size_before = path.size();
        path.push_back(PathEntry{&node, index});
        if (select_path(*node.children[index], path, bb, tc))
            return true;
        path.resize(size_before); // 当前相位子节点条件失败：整序不可运行（由上层回退/重评）
        return false;
    }
    }
    return false;
}

void BtDecisionMaker::advance_done(std::vector<PathEntry>& path, const core::Blackboard& bb,
                                   const TickContext& tc) {
    // 守卫：每轮推进一层，最多推进路径深度次，防死循环。
    for (std::size_t guard = 0; guard < path.size(); ++guard) {
        const BtNode* leaf = path.back().node;
        if (!leaf->has_done_when || !done_when_satisfied(*leaf, bb, tc))
            return;
        // 最近的 sequence 父（done_when 解析期已保证直接父为 sequence）。
        std::size_t seq = path.size();
        for (std::size_t i = path.size() - 1; i > 0; --i) {
            if (path[i - 1].node->kind == BtNodeKind::Sequence) {
                seq = i - 1;
                break;
            }
        }
        if (seq == path.size())
            return; // 解析期已校验，不会发生
        auto& entry = path[seq];
        entry.child_index = (entry.child_index + 1) % entry.node->children.size();
        path.resize(seq + 1);
        std::vector<PathEntry> tail;
        if (!select_path(*entry.node->children[entry.child_index], tail, bb, tc))
            return; // 新相位不可运行：留空（调用方下一 tick 重评）
        for (auto& e : tail)
            path.push_back(std::move(e));
        phase_started_at_ = tc.game_time;
    }
}

bool BtDecisionMaker::done_when_satisfied(const BtNode& leaf, const core::Blackboard& bb,
                                          const TickContext& tc) const {
    // elapsed_ge 以当前相位起始时刻为基准（1e-9 容差抵御浮点累计误差）；bb 走共享求值。
    for (auto it = leaf.done_when.begin(); it != leaf.done_when.end(); ++it) {
        if (it.key() == "elapsed_ge") {
            if (!it->is_number() || tc.game_time - phase_started_at_ + 1e-9 < it->get<double>())
                return false;
        }
    }
    return internal::evaluate_conditions(leaf.done_when, bb, {"elapsed_ge"});
}

void BtDecisionMaker::reset_runtime(const TickContext& tc) {
    path_.clear();
    started_ = false;
    phase_started_at_ = tc.game_time;
}

} // namespace npc_agent::decision
