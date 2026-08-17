#include "npc_agent/decision/fsm_decision_maker.h"

#include <utility>

#include "condition.h"
#include "intent_spec.h"

namespace npc_agent::decision {

namespace {

using internal::parse_intent_spec;

} // namespace

std::optional<std::string> parse_fsm_definition(const json& in, FsmDefinition& out) {
    FsmDefinition def;
    if (!in.is_object())
        return std::string("FSM 定义应为对象");
    const auto initial_it = in.find("initial");
    if (initial_it == in.end() || !initial_it->is_string())
        return std::string("FSM 定义缺少 initial 字符串");
    def.initial = initial_it->get<std::string>();
    const auto states_it = in.find("states");
    if (states_it == in.end() || !states_it->is_array() || states_it->empty())
        return std::string("FSM 定义缺少非空 states 数组");

    for (std::size_t i = 0; i < states_it->size(); ++i) {
        const json& s = (*states_it)[i];
        const std::string where = "states[" + std::to_string(i) + "]";
        if (!s.is_object())
            return where + " 应为对象";
        const auto name_it = s.find("name");
        if (name_it == s.end() || !name_it->is_string() || name_it->get<std::string>().empty())
            return where + ".name 应为非空字符串";
        FsmStateDef state;
        state.name = name_it->get<std::string>();
        // 状态名唯一性
        for (const auto& existing : def.states) {
            if (existing.name == state.name)
                return where + ".name 重复: " + state.name;
        }
        if (auto it = s.find("priority"); it != s.end()) {
            if (!it->is_number())
                return where + ".priority 应为数值";
            state.priority = it->get<float>();
        }
        if (auto it = s.find("intent"); it != s.end()) {
            if (auto err = parse_intent_spec(*it, state.intent); err.has_value())
                return where + "." + *err;
        } else {
            state.intent = std::nullopt; // 缺省 idle
        }
        if (auto it = s.find("transitions"); it != s.end()) {
            if (!it->is_array())
                return where + ".transitions 应为数组";
            for (std::size_t t = 0; t < it->size(); ++t) {
                const json& tr = (*it)[t];
                const std::string twhere = where + ".transitions[" + std::to_string(t) + "]";
                if (!tr.is_object())
                    return twhere + " 应为对象";
                const auto target_it = tr.find("target");
                if (target_it == tr.end() || !target_it->is_string() ||
                    target_it->get<std::string>().empty())
                    return twhere + ".target 应为非空字符串";
                FsmTransition transition;
                transition.target = target_it->get<std::string>();
                if (auto cond_it = tr.find("condition"); cond_it != tr.end()) {
                    if (!cond_it->is_object())
                        return twhere + ".condition 应为对象";
                    if (!internal::condition_keys_allowed(*cond_it,
                                                          {"bb", "elapsed_ge", "default"}))
                        return twhere + ".condition 含未知键（允许 bb/elapsed_ge/default）";
                    transition.condition = *cond_it;
                }
                state.transitions.push_back(std::move(transition));
            }
        }
        def.states.push_back(std::move(state));
    }

    // 迁移目标存在性校验
    for (const auto& state : def.states) {
        for (const auto& transition : state.transitions) {
            bool found = false;
            for (const auto& candidate : def.states)
                found = found || (candidate.name == transition.target);
            if (!found)
                return "状态 " + state.name + " 的迁移目标不存在: " + transition.target;
        }
    }
    // initial 存在性校验
    bool initial_found = false;
    for (const auto& candidate : def.states)
        initial_found = initial_found || (candidate.name == def.initial);
    if (!initial_found)
        return "initial 状态不存在: " + def.initial;

    out = std::move(def);
    return std::nullopt;
}

FsmDecisionMaker::FsmDecisionMaker(FsmDefinition def) : def_(std::move(def)) {
    current_ = find_state(def_.initial);
    // entered_at_ 保持 0：首个 tick 前未开始（首次 propose 时补记进入时刻）。
}

std::optional<Intent> FsmDecisionMaker::propose(const core::Blackboard& bb, const TickContext& tc) {
    if (current_ == nullptr)
        return std::nullopt;
    if (!started_) {
        // 首 tick：只记录进入时刻，不评估迁移（状态至少驻留一个完整 tick，
        // 迁移序列与 trace 逐 tick 对齐，确定性更强）。
        entered_at_ = tc.game_time;
        started_ = true;
    } else {
        // 迁移评估：按声明顺序取首个命中（条件互斥是定义方责任）。
        for (const auto& transition : current_->transitions) {
            if (evaluate(transition.condition, bb, tc)) {
                current_ = find_state(transition.target); // 定义已校验，必然存在
                entered_at_ = tc.game_time;
                break;
            }
        }
    }
    if (current_->intent.has_value()) {
        Intent intent = *current_->intent; // 模板拷贝（阶段 6 池化目标，CS-§7.5 备忘）
        intent.priority = current_->priority;
        return intent;
    }
    return std::nullopt; // idle：无权威意图，模块候选接管
}

void FsmDecisionMaker::to_json(json& out) const {
    out = json::object();
    if (current_ != nullptr) {
        out = json{{"current", current_->name}, {"entered_at", entered_at_}};
    }
}

void FsmDecisionMaker::from_json(const json& in) {
    current_ = nullptr;
    entered_at_ = 0.0;
    started_ = false;
    if (!in.is_object())
        return;
    const auto name = in.value("current", "");
    if (name.empty())
        return;
    current_ = find_state(name); // 名称无效则保持未启动（定义变更场景）
    entered_at_ = in.value("entered_at", 0.0);
    started_ = current_ != nullptr; // 恢复后继续按已进入状态评估迁移
}

std::string_view FsmDecisionMaker::current_state() const {
    return current_ != nullptr ? current_->name : std::string_view{};
}

bool FsmDecisionMaker::evaluate(const json& condition, const core::Blackboard& bb,
                                const TickContext& tc) const {
    // bb/default 走共享求值（未知键恒假）；elapsed_ge 为 FSM 专属（状态停留时长），
    // 加 1e-9 容差抵御浮点累计误差（如 dt=0.1 的累加，确定性不受影响）。
    for (auto it = condition.begin(); it != condition.end(); ++it) {
        if (it.key() == "elapsed_ge") {
            if (!it->is_number())
                return false;
            if (tc.game_time - entered_at_ + 1e-9 < it->get<double>())
                return false;
        }
    }
    return internal::evaluate_conditions(condition, bb, {"elapsed_ge"});
}

const FsmStateDef* FsmDecisionMaker::find_state(std::string_view name) const {
    for (const auto& state : def_.states) {
        if (state.name == name)
            return &state;
    }
    return nullptr;
}

} // namespace npc_agent::decision
