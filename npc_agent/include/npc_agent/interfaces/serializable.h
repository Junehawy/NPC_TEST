// 序列化契约（RA-§7.3）：阶段 1 只定义接口，具体 schema 随模块交付。
// ICapability 与 Blackboard 实现本契约；Agent 因恢复需要工厂/身体，使用自身的 to_json/restore 对。
#pragma once

#include <nlohmann/json.hpp>

namespace npc_agent {

struct ISerializable {
    virtual ~ISerializable() = default;
    // 写入状态（输出对象归调用方所有）；与 from_json 必须往返等价（TS-§3）。
    virtual void to_json(nlohmann::json& out) const = 0;
    // 读入状态。前置：in 结构已由恢复调用方校验（编程错误用 assert 拦截）。
    virtual void from_json(const nlohmann::json& in) = 0;
};

} // namespace npc_agent
