// GridNav 单元测试（TS-§3 scenarios 行：直行/绕障/不可达/确定性/版本号）。
#include <catch2/catch_test_macros.hpp>

#include "npc_agent/testing/grid_nav.h"

using namespace npc_agent;
using namespace npc_agent::testing;

namespace {
// 20×10 网格，单元 1.0 世界单位。
GridNav make_grid() {
    return GridNav(20, 10, 1.0f);
}

Vec3 cell_center(int col, int row) {
    return GridNav(1, 1, 1.0f).cell_to_world(col, row);
}

// Vec3 为纯 POD（接口层最小化，无 operator==），测试用字段级比较。
bool same_pos(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
bool paths_equal(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!same_pos(a[i], b[i]))
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("GridNav 无障碍直行：路径为直线航点且不含起点含终点", "[grid_nav]") {
    auto grid = make_grid();
    const Vec3 from = cell_center(1, 5);
    const Vec3 to = cell_center(8, 5);
    const auto path = grid.find_path(from, to);
    REQUIRE(!path.empty());
    REQUIRE(path.size() == 7); // 1..8 共 7 个中间/终点单元
    REQUIRE(same_pos(path.back(), to));
    // 全部航点不在起点单元
    for (const auto& wp : path) {
        const auto cell = grid.world_to_cell(wp);
        REQUIRE(grid.in_bounds(cell->first, cell->second));
    }
    REQUIRE(grid.can_reach(from, to));
}

TEST_CASE("GridNav 障碍绕行：路径避开阻塞单元且仍可达", "[grid_nav]") {
    auto grid = make_grid();
    // 在 (4..7, 5) 放置竖直障碍墙，阻断直线 (1,5)→(8,5)。
    for (int c = 4; c <= 7; ++c)
        grid.set_obstacle(c, 5, true);
    const Vec3 from = cell_center(1, 5);
    const Vec3 to = cell_center(8, 5);
    const auto path = grid.find_path(from, to);
    REQUIRE(!path.empty());
    for (const auto& wp : path) {
        const auto cell = grid.world_to_cell(wp);
        REQUIRE_FALSE(grid.is_blocked(cell->first, cell->second));
    }
    REQUIRE(same_pos(path.back(), to));
    REQUIRE(grid.can_reach(from, to));
}

TEST_CASE("GridNav 不可达：封闭区域返回空路径且 can_reach=false", "[grid_nav]") {
    auto grid = make_grid();
    // 用墙把 (5,5) 围起来。
    grid.set_obstacle(4, 5, true);
    grid.set_obstacle(6, 5, true);
    grid.set_obstacle(5, 4, true);
    grid.set_obstacle(5, 6, true);
    const Vec3 inside = cell_center(5, 5);
    const Vec3 outside = cell_center(0, 0);
    REQUIRE(grid.find_path(inside, outside).empty());
    REQUIRE_FALSE(grid.can_reach(inside, outside));
    REQUIRE(grid.find_path(outside, inside).empty());
}

TEST_CASE("GridNav 起终点阻塞：返回空路径", "[grid_nav]") {
    auto grid = make_grid();
    grid.set_obstacle(3, 3, true);
    REQUIRE(grid.find_path(cell_center(3, 3), cell_center(9, 9)).empty());
    grid.set_obstacle(9, 9, true);
    REQUIRE(grid.find_path(cell_center(0, 0), cell_center(9, 9)).empty());
}

TEST_CASE("GridNav 同单元：无需移动（空路径）", "[grid_nav]") {
    auto grid = make_grid();
    REQUIRE(grid.find_path(cell_center(2, 2), cell_center(2, 2)).empty());
    REQUIRE(grid.can_reach(cell_center(2, 2), cell_center(2, 2)));
}

TEST_CASE("GridNav 确定性：同网格同起终点两次结果一致", "[grid_nav]") {
    auto grid = make_grid();
    for (int c = 6; c <= 9; ++c)
        grid.set_obstacle(c, 4, true);
    grid.set_obstacle(7, 5, true);
    const Vec3 from = cell_center(1, 1);
    const Vec3 to = cell_center(12, 6);
    const auto a = grid.find_path(from, to);
    const auto b = grid.find_path(from, to);
    REQUIRE(paths_equal(a, b));
    REQUIRE(a.size() >= 2);
}

TEST_CASE("GridNav 障碍版本号：实际变更自增，重复设置不增", "[grid_nav]") {
    auto grid = make_grid();
    const std::uint64_t v0 = grid.obstacle_version();
    REQUIRE(grid.set_obstacle(2, 2, true));
    REQUIRE(grid.obstacle_version() == v0 + 1);
    REQUIRE_FALSE(grid.set_obstacle(2, 2, true)); // 幂等
    REQUIRE(grid.obstacle_version() == v0 + 1);
    REQUIRE(grid.set_obstacle(2, 2, false));
    REQUIRE(grid.obstacle_version() == v0 + 2);
    REQUIRE_FALSE(grid.set_obstacle(-1, -1, true)); // 越界
    REQUIRE(grid.obstacle_version() == v0 + 2);
}

TEST_CASE("GridNav 世界坐标 ↔ 单元换算", "[grid_nav]") {
    auto grid = make_grid();
    const auto cell = grid.world_to_cell(cell_center(7, 3));
    REQUIRE(cell.has_value());
    REQUIRE(cell->first == 7);
    REQUIRE(cell->second == 3);
    REQUIRE(!grid.world_to_cell(Vec3{-3.0f, 0.0f, 0.0f}).has_value()); // 越界
}
