// MockWorld —— 无头模拟世界（RA-§8.2 testing/）：IWorld 的测试实现，测试与示例共用。
// 位置说明（R6-1）：因依赖方向约束（npc_agent/tests 不得 include game_adapter），
// 模拟宿主实现置于框架 testing 模块，game_adapter 仅保留 sample_adapter。
// 复杂度说明：实体/刺激为测试量级，线性扫描即可；生产规模的空间分区属宿主职责（RA-§3.2）。
#pragma once

#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "npc_agent/interfaces/i_world.h"

namespace npc_agent::testing {

class MockWorld final : public IWorld {
public:
    MockWorld() = default;

    // ---- 测试驱动（【驱动线程】，与 IWorld 一致） ----
    void advance(double dt); // 推进时间并 tick_index++
    void add_entity(std::string id, Vec3 pos, json attributes = json::object());
    void move_entity(std::string_view id, Vec3 pos); // 未知 id 无操作
    std::optional<Vec3> entity_pos(std::string_view id) const;
    void set_reachable(bool reachable);               // can_reach/find_path 统一开关
    const std::vector<Stimulus>& stimuli_log() const; // 断言用刺激记录

    // ---- IWorld（RA-§3.2） ----
    TickContext tick_context() const override;
    PerceptionResult sense(const PerceptionQuery& q) const override;
    void inject_stimulus(const Stimulus& s) override;
    bool can_reach(Vec3 from, Vec3 to) const override;
    std::vector<Vec3> find_path(Vec3 from, Vec3 to) const override;
    WorldSnapshot snapshot() const override;

private:
    struct Entity {
        std::string id;
        Vec3 pos;
        json attributes;
    };

    double game_time_ = 0.0;
    double last_dt_ = 0.0;
    uint64_t tick_index_ = 0;
    std::vector<Entity> entities_;
    std::vector<Stimulus> stimuli_log_;
    bool reachable_ = true;
    std::deque<MemoryEvent> recent_; // 快照用近期事件环（容量 32）
};

} // namespace npc_agent::testing
