#include "register_types.h"

#include <godot_cpp/core/class_db.hpp>

#include "npc_agent_demo_node.h"

namespace npc_agent::adapter::godot_demo {

void initialize_demo_module(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE)
        return;
    godot::ClassDB::register_class<NpcAgentDemoNode>();
}

void uninitialize_demo_module(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE)
        return;
    // 演示类由 ClassDB 管理，无自持静态资源，无需额外清理。
}

} // namespace npc_agent::adapter::godot_demo
