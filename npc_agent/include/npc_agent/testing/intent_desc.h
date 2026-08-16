// intent_desc —— 意图可读描述（npc_agent/testing/）：示例与调试输出的共用工具，
// 非框架运行时逻辑（不参与仲裁/执行路径）。
// 线程契约：【任意线程】（纯函数，只读入参）。
#pragma once

#include <optional>
#include <string>

#include "npc_agent/interfaces/intent.h"

namespace npc_agent::testing {

// 将仲裁胜出意图转为单行可读文本（日志/调试面板用）；空值返回"无意图"。
std::string describe_intent(const std::optional<Intent>& intent);

} // namespace npc_agent::testing
