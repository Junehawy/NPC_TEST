// WorldTransform —— 世界坐标 ↔ Godot 像素坐标换算（game_adapter/godot_demo 内部工具）。
// 约定：1 世界单位 = scale 像素；世界原点位于屏幕 origin（像素）；
// 框架 Vec3 的 x/y 对应 2D 场景的 x/y，z 恒为 0。
// 线程契约：【驱动线程】（纯值换算，无状态写入）。
#pragma once

#include <godot_cpp/variant/vector2.hpp>

#include "npc_agent/interfaces/types.h"

namespace npc_agent::adapter::godot_demo {

struct WorldTransform {
    float scale = 100.0f;    // 像素 / 世界单位
    godot::Vector2 origin{}; // 世界原点所在屏幕位置（像素）

    Vec3 to_world(godot::Vector2 px) const {
        return Vec3{(px.x - origin.x) / scale, (px.y - origin.y) / scale, 0.0f};
    }

    godot::Vector2 to_pixel(Vec3 world) const {
        return godot::Vector2(origin.x + world.x * scale, origin.y + world.y * scale);
    }
};

} // namespace npc_agent::adapter::godot_demo
