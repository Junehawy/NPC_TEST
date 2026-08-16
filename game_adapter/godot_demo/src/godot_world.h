// GodotWorld —— Godot 2D 示例的 IWorld 实现（game_adapter 层，RA-§3.2）：
// 把场景树里的"可感知实体"（Node2D）投影为框架世界坐标，驱动 TickContext 时间推进，
// 并记录刺激日志。实体量为演示量级，线性扫描即可（生产级空间分区属宿主职责）。
// 线程契约：全部方法【驱动线程】（Godot 主线程），与 IWorld 一致。
#pragma once

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <godot_cpp/classes/node2d.hpp>

#include "godot_transform.h"
#include "npc_agent/interfaces/i_world.h"

namespace npc_agent::adapter::godot_demo {

class GodotWorld final : public IWorld {
public:
    // 坐标换算参数（演示装配期调用）。
    void set_transform(WorldTransform transform);

    // 注册可感知实体（演示装配期调用；示例不支持运行时注销）。
    void add_entity(std::string id, godot::Node2D* node);

    // 推进世界时间（每帧 delta 秒；tick_index 单调递增）。
    void advance(double dt);

    // ---- IWorld（RA-§3.2） ----
    TickContext tick_context() const override;
    PerceptionResult sense(const PerceptionQuery& q) const override;
    void inject_stimulus(const Stimulus& s) override;          // 记录日志；广播由 AgentSystem 负责
    bool can_reach(Vec3, Vec3) const override;                 // 示例无障碍，恒可达
    std::vector<Vec3> find_path(Vec3, Vec3 to) const override; // 示例无导航，直达
    WorldSnapshot snapshot() const override;

    // 观察（调试面板 / 刺激注入用）。
    std::optional<Vec3> entity_pos(std::string_view id) const; // 未知 id 返回 nullopt
    const std::vector<Stimulus>& stimuli_log() const;

private:
    struct Entity {
        std::string id;
        godot::Node2D* node = nullptr;
    };

    WorldTransform transform_;
    double game_time_ = 0.0;
    double last_dt_ = 0.0;
    uint64_t tick_index_ = 0;
    std::vector<Entity> entities_;
    std::vector<Stimulus> stimuli_log_;
    std::deque<MemoryEvent> recent_; // 快照用近期事件环（容量 32，与 MockWorld 一致）
};

} // namespace npc_agent::adapter::godot_demo
