#include "npc_agent/decision/utility_decision_maker.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "condition.h"
#include "intent_spec.h"

namespace npc_agent::decision {

namespace {

// FNV-1a 64 位（跨平台稳定，trace 复现前提，RA-§3.7）。
uint64_t fnv1a(std::string_view text) {
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

// splitmix64：把种子混合为高质量均匀位（确定性伪随机，非加密用途）。
uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// 选项噪声：[-amplitude, +amplitude] 均匀分布，仅由 rng_seed + 选项名决定。
float option_noise(const UtilityOption& option, uint64_t rng_seed) {
    if (option.noise_amplitude <= 0.0f)
        return 0.0f;
    const uint64_t mixed = splitmix64(rng_seed ^ fnv1a(option.name));
    const double unit = static_cast<double>(mixed >> 11) * 0x1.0p-53; // [0, 1)
    return static_cast<float>((unit * 2.0 - 1.0) * option.noise_amplitude);
}

} // namespace

std::optional<std::string> parse_utility_definition(const json& in, UtilityDefinition& out) {
    UtilityDefinition def;
    if (!in.is_object())
        return std::string("Utility 定义应为对象");
    const auto options_it = in.find("options");
    if (options_it == in.end() || !options_it->is_array())
        return std::string("Utility 定义缺少 options 数组");

    for (std::size_t i = 0; i < options_it->size(); ++i) {
        const json& o = (*options_it)[i];
        const std::string where = "options[" + std::to_string(i) + "]";
        if (!o.is_object())
            return where + " 应为对象";
        const auto name_it = o.find("name");
        if (name_it == o.end() || !name_it->is_string() || name_it->get<std::string>().empty())
            return where + ".name 应为非空字符串";
        UtilityOption option;
        option.name = name_it->get<std::string>();
        for (const auto& existing : def.options) {
            if (existing.name == option.name)
                return where + ".name 重复: " + option.name;
        }
        if (auto it = o.find("base_score"); it != o.end()) {
            if (!it->is_number())
                return where + ".base_score 应为数值";
            option.base_score = it->get<float>();
        }
        if (auto it = o.find("noise_amplitude"); it != o.end()) {
            if (!it->is_number() || it->get<float>() < 0.0f)
                return where + ".noise_amplitude 应为非负数值";
            option.noise_amplitude = it->get<float>();
        }
        if (auto it = o.find("condition"); it != o.end()) {
            if (!it->is_object())
                return where + ".condition 应为对象";
            if (!internal::condition_keys_allowed(*it, {"bb", "default"}))
                return where + ".condition 含未知键（允许 bb/default）";
            option.condition = *it;
        }
        if (auto it = o.find("intent"); it != o.end()) {
            if (auto err = internal::parse_intent_spec(*it, option.intent); err.has_value())
                return where + "." + *err;
        } else {
            option.intent = std::nullopt; // 缺省待机
        }
        def.options.push_back(std::move(option));
    }

    out = std::move(def);
    return std::nullopt;
}

UtilityDecisionMaker::UtilityDecisionMaker(UtilityDefinition def) : def_(std::move(def)) {
}

std::optional<Intent> UtilityDecisionMaker::propose(const core::Blackboard& bb,
                                                    const TickContext& tc) {
    last_picked_.clear();
    last_score_ = 0.0f;
    const UtilityOption* best = nullptr;
    float best_score = -std::numeric_limits<float>::infinity();

    for (const auto& option : def_.options) {
        if (!internal::evaluate_conditions(option.condition, bb))
            continue;
        const float score = option.base_score + option_noise(option, tc.rng_seed);
        if (best == nullptr || score > best_score) { // 同分保留先声明者（确定性）
            best = &option;
            best_score = score;
        }
    }
    if (best == nullptr)
        return std::nullopt; // 无选项命中
    last_picked_ = best->name;
    last_score_ = best_score;
    if (!best->intent.has_value())
        return std::nullopt; // 待机选项胜出：无权威意图，模块候选接管
    return *best->intent;    // 模板拷贝（阶段 6 池化目标，CS-§7.5 备忘）
}

void UtilityDecisionMaker::to_json(json& out) const {
    out = json{{"last_picked", last_picked_}, {"last_score", last_score_}};
}

void UtilityDecisionMaker::from_json(const json& in) {
    if (!in.is_object())
        return;
    last_picked_ = in.value("last_picked", "");
    last_score_ = in.value("last_score", 0.0f);
}

std::string_view UtilityDecisionMaker::last_picked() const {
    return last_picked_;
}

float UtilityDecisionMaker::last_score() const {
    return last_score_;
}

} // namespace npc_agent::decision
