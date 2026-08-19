// GridNav —— 宿主侧 A* 导航示例（npc_agent/testing/，阶段 3，RA 路线图 3.x 第 1 条）。
// 网格占用 + A* 寻路：纯逻辑、无第三方依赖，作为 headless 测试设施；
// Godot 演示复用为真实宿主导航（IWorld::find_path/can_reach 的宿主实现示例）。
// 确定性（RA-§3.7）：A* 候选按 (f, g, 入队序) 严格排序，无平局随机；
// 相同网格与起终点 → 相同路径（trace/回放友好）。
// 世界坐标 ↔ 单元：world = cell * cell_size + cell_size/2（单元中心）。
// 复杂度：O(E log E)，E 为网格单元数（演示网格 ≤ 48×27，单次寻路 <0.1ms；
// 生产级大图的宿主异步化策略见 RA 开放问题 #10 评估 R10）。
// 线程契约：全部方法【驱动线程】。
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "npc_agent/interfaces/types.h"

namespace npc_agent::testing {

class GridNav {
public:
    GridNav(int width_cells, int height_cells, float cell_size);

    int width() const;
    int height() const;
    float cell_size() const;

    // 障碍设置：越界忽略；返回是否实际变更（驱动障碍版本号，宿主路径缓存失效用）。
    bool set_obstacle(int col, int row, bool blocked);
    // 批量阻塞矩形（含边界，场景障碍登记用）。
    void block_rect(int col0, int row0, int col1, int row1);
    bool is_blocked(int col, int row) const;
    bool in_bounds(int col, int row) const;

    // 世界原点（cell(0,0) 的世界坐标）：演示场景世界原点通常在屏幕中心，
    // 坐标为负也可正确映射（阶段 3 演示）。默认 (0,0,0)。
    void set_origin(Vec3 origin);
    Vec3 origin() const;

    // 障碍版本号：任何 set_obstacle 实际变更后自增（宿主可按此做路径缓存失效）。
    std::uint64_t obstacle_version() const;

    // 世界坐标 ↔ 单元（越界返回 nullopt）。
    std::optional<std::pair<int, int>> world_to_cell(Vec3 world) const;
    Vec3 cell_to_world(int col, int row) const;

    // 寻路（四方向移动，曼哈顿启发式；A* 确定性排序）。
    // 起点/终点取所属单元中心；起点阻塞或不可达返回 false / 空路径。
    bool can_reach(Vec3 from, Vec3 to) const;
    // 返回世界坐标航点：不含起点、含终点；沿路径依次经过（路径平滑属阶段 6 打磨项）。
    std::vector<Vec3> find_path(Vec3 from, Vec3 to) const;

private:
    // A* 开放表项（按 f/g/行列全序排序，确定性无平局随机）。
    struct OpenEntry {
        int f;
        int g;
        int col;
        int row;
        bool operator<(const OpenEntry& other) const {
            if (f != other.f)
                return f < other.f;
            if (g != other.g)
                return g < other.g;
            if (col != other.col)
                return col < other.col;
            return row < other.row;
        }
    };

    int index(int col, int row) const;

    int width_;
    int height_;
    float cell_size_;
    Vec3 origin_{};
    std::vector<std::uint8_t> blocked_; // 1 = 阻塞
    std::uint64_t version_ = 0;
};

} // namespace npc_agent::testing
