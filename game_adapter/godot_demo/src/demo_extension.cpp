// GDExtension 入口：供 Godot 运行时加载本扩展（entry_symbol 见
// npc_agent_godot_demo.gdextension）。初始化级别为 SCENE（只需场景类）。
#include <gdextension_interface.h>

#include <godot_cpp/godot.hpp>

#include "register_types.h"

extern "C" {

// Godot 加载扩展时调用的入口函数；返回初始化结果。
GDExtensionBool
npc_agent_godot_demo_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                  GDExtensionClassLibraryPtr library,
                                  GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
    init_obj.register_initializer(npc_agent::adapter::godot_demo::initialize_demo_module);
    init_obj.register_terminator(npc_agent::adapter::godot_demo::uninitialize_demo_module);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

} // extern "C"
