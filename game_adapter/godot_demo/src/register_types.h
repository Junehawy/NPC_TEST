// 演示模块注册：把 NpcAgentDemoNode 注册进 ClassDB，供场景按类名实例化。
// 线程契约：【驱动线程】（引擎启动时调用）。
#pragma once

#include <godot_cpp/godot.hpp>

namespace npc_agent::adapter::godot_demo {

void initialize_demo_module(godot::ModuleInitializationLevel level);
void uninitialize_demo_module(godot::ModuleInitializationLevel level);

} // namespace npc_agent::adapter::godot_demo
