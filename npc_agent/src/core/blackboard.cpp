#include "npc_agent/core/blackboard.h"

#include <utility>

namespace npc_agent::core {

void Blackboard::set(std::string_view key, nlohmann::json value) {
    entries_[std::string(key)] = std::move(value);
}

const nlohmann::json* Blackboard::get(std::string_view key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

bool Blackboard::contains(std::string_view key) const {
    return entries_.find(key) != entries_.end();
}

void Blackboard::erase(std::string_view key) {
    // 注：map::erase(异构键) 需 C++23；C++20 下先 find 再按迭代器删除（同样 O(log n)）
    const auto it = entries_.find(key);
    if (it != entries_.end())
        entries_.erase(it);
}

std::size_t Blackboard::size() const noexcept {
    return entries_.size();
}

void Blackboard::clear() noexcept {
    entries_.clear();
}

const std::map<std::string, nlohmann::json, std::less<>>& Blackboard::entries() const noexcept {
    return entries_;
}

void Blackboard::to_json(nlohmann::json& out) const {
    out = entries_;
}

void Blackboard::from_json(const nlohmann::json& in) {
    entries_.clear();
    if (!in.is_object())
        return; // 前置：结构已由恢复调用方校验（CS-§9）
    for (const auto& [key, value] : in.items()) {
        entries_.emplace(key, value);
    }
}

} // namespace npc_agent::core
