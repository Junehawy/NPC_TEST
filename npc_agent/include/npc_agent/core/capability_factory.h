// CapabilityFactory —— 能力注册表（存档恢复用，RA-§7.3）。
// 能力模块经 id 注册构造器；Agent::restore 按存档中的 id 重建实例并回填状态。
// 线程契约：【驱动线程】调用（注册发生在装配期，单线程）。
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "npc_agent/interfaces/icapability.h"

namespace npc_agent::core {

class CapabilityFactory {
public:
    using Creator = std::function<std::unique_ptr<ICapability>()>;

    // 注册/覆盖构造器（后注册覆盖先注册；同 id 以最终注册为准）。
    void register_creator(std::string id, Creator creator);

    // 创建实例；id 未注册返回 nullptr（调用方负责报错定位）。
    [[nodiscard]] std::unique_ptr<ICapability> create(std::string_view id) const;

    [[nodiscard]] bool knows(std::string_view id) const;

private:
    std::map<std::string, Creator, std::less<>> creators_;
};

} // namespace npc_agent::core
