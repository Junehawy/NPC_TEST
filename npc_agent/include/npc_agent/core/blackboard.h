// Blackboard —— 每 Agent 持续可读状态（RA-§4）：跨 tick 存活，参与序列化。
// 线程契约：【驱动线程】访问（非线程安全，RA-§2.2）。
// 值类型：nlohmann::json；透明比较支持 string_view 键查询（免临时 std::string）。
#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "npc_agent/interfaces/serializable.h"

namespace npc_agent::core {

class Blackboard : public ISerializable {
public:
    // 写入/覆盖键值。复杂度 O(log n)。
    void set(std::string_view key, nlohmann::json value);

    // 读取；键不存在返回 nullptr（指针在下次写入前有效）。
    const nlohmann::json* get(std::string_view key) const;

    bool contains(std::string_view key) const; // O(log n)
    void erase(std::string_view key);          // 不存在时无操作
    std::size_t size() const noexcept;
    void clear() noexcept;

    // 只读遍历（仲裁/快照诊断用）。
    const std::map<std::string, nlohmann::json, std::less<>>& entries() const noexcept;

    // ISerializable：整体序列化为 JSON 对象；from_json 前置结构已校验。
    void to_json(nlohmann::json& out) const override;
    void from_json(const nlohmann::json& in) override;

private:
    std::map<std::string, nlohmann::json, std::less<>> entries_;
};

} // namespace npc_agent::core
