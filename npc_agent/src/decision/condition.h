// condition —— 条件求值共享件（decision/ 模块内部共享，非公共 API）。
// 黑板条件求值：bb 键比对 + default 兜底；未知键恒假（运行期安全拒绝）。
#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "npc_agent/core/blackboard.h"

namespace npc_agent::decision::internal {

// 求值 {"bb": {key: 期望值...}, "default": bool}（AND 语义；未知键 → false）。
// 空条件对象 = 恒真。skip_keys 中的键由调用方自行处理（如 FSM 的 elapsed_ge）。
inline bool evaluate_conditions(const nlohmann::json& condition, const core::Blackboard& bb,
                                std::initializer_list<const char*> skip_keys = {}) {
    for (auto it = condition.begin(); it != condition.end(); ++it) {
        bool skipped = false;
        for (const char* key : skip_keys)
            skipped = skipped || (it.key() == key);
        if (skipped)
            continue;
        if (it.key() == "bb") {
            if (!it->is_object())
                return false;
            for (auto b = it->begin(); b != it->end(); ++b) {
                const nlohmann::json* value = bb.get(b.key());
                if (value == nullptr || *value != *b)
                    return false;
            }
        } else if (it.key() == "default") {
            if (!it->is_boolean() || !it->get<bool>())
                return false;
        } else {
            return false; // 未知条件键：恒假
        }
    }
    return true;
}

// 检查条件对象仅含允许的键（解析期 fail-fast 用）。
inline bool condition_keys_allowed(const nlohmann::json& condition,
                                   std::initializer_list<const char*> allowed) {
    if (!condition.is_object())
        return false;
    for (auto it = condition.begin(); it != condition.end(); ++it) {
        bool ok = false;
        for (const char* key : allowed)
            ok = ok || (it.key() == key);
        if (!ok)
            return false;
    }
    return true;
}

} // namespace npc_agent::decision::internal
