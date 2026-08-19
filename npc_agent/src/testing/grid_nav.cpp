#include "npc_agent/testing/grid_nav.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace npc_agent::testing {

namespace {
// 四方向邻接（上下左右；无斜向，确定性最简）。
constexpr int kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

int manhattan(int c0, int r0, int c1, int r1) {
    return std::abs(c0 - c1) + std::abs(r0 - r1);
}
} // namespace

GridNav::GridNav(int width_cells, int height_cells, float cell_size)
    : width_(width_cells), height_(height_cells), cell_size_(cell_size),
      blocked_(static_cast<std::size_t>(width_cells) * height_cells, 0) {
}

int GridNav::width() const {
    return width_;
}

int GridNav::height() const {
    return height_;
}

float GridNav::cell_size() const {
    return cell_size_;
}

int GridNav::index(int col, int row) const {
    return row * width_ + col;
}

bool GridNav::set_obstacle(int col, int row, bool blocked) {
    if (!in_bounds(col, row))
        return false;
    const std::uint8_t value = blocked ? 1 : 0;
    if (blocked_[index(col, row)] == value)
        return false;
    blocked_[index(col, row)] = value;
    ++version_;
    return true;
}

void GridNav::block_rect(int col0, int row0, int col1, int row1) {
    for (int r = row0; r <= row1; ++r)
        for (int c = col0; c <= col1; ++c)
            set_obstacle(c, r, true);
}

bool GridNav::is_blocked(int col, int row) const {
    return in_bounds(col, row) && blocked_[index(col, row)] != 0;
}

bool GridNav::in_bounds(int col, int row) const {
    return col >= 0 && col < width_ && row >= 0 && row < height_;
}

std::uint64_t GridNav::obstacle_version() const {
    return version_;
}

void GridNav::set_origin(Vec3 origin) {
    origin_ = origin;
}

Vec3 GridNav::origin() const {
    return origin_;
}

std::optional<std::pair<int, int>> GridNav::world_to_cell(Vec3 world) const {
    const int col = static_cast<int>(std::floor((world.x - origin_.x) / cell_size_));
    const int row = static_cast<int>(std::floor((world.y - origin_.y) / cell_size_));
    if (!in_bounds(col, row))
        return std::nullopt;
    return std::pair<int, int>{col, row};
}

Vec3 GridNav::cell_to_world(int col, int row) const {
    return Vec3{origin_.x + (static_cast<float>(col) + 0.5f) * cell_size_,
                origin_.y + (static_cast<float>(row) + 0.5f) * cell_size_, 0.0f};
}

bool GridNav::can_reach(Vec3 from, Vec3 to) const {
    const auto start = world_to_cell(from);
    const auto goal = world_to_cell(to);
    if (!start.has_value() || !goal.has_value())
        return false;
    if (start == goal)
        return true; // 已就位：无需移动即可达
    return !find_path(from, to).empty();
}

std::vector<Vec3> GridNav::find_path(Vec3 from, Vec3 to) const {
    std::vector<Vec3> waypoints;
    const auto start = world_to_cell(from);
    const auto goal = world_to_cell(to);
    if (!start.has_value() || !goal.has_value())
        return waypoints;
    if (is_blocked(start->first, start->second) || is_blocked(goal->first, goal->second))
        return waypoints;
    if (start == goal)
        return waypoints; // 已就位：无路径（零航点 = 不需要移动）

    // A*：确定性开放表（f/g/行列全序）+ 父节点回溯。
    const int h0 = manhattan(start->first, start->second, goal->first, goal->second);
    std::set<OpenEntry> open;
    std::vector<std::pair<int, int>> parent(blocked_.size(), {-1, -1});
    std::vector<int> best_g(blocked_.size(), -1);
    open.insert(OpenEntry{h0, 0, start->first, start->second});
    best_g[index(start->first, start->second)] = 0;

    int goal_col = goal->first;
    int goal_row = goal->second;
    bool found = false;
    while (!open.empty() && !found) {
        const OpenEntry current = *open.begin();
        open.erase(open.begin());
        if (current.col == goal_col && current.row == goal_row) {
            found = true;
            break;
        }
        for (const auto& dir : kDirs) {
            const int nc = current.col + dir[0];
            const int nr = current.row + dir[1];
            if (!in_bounds(nc, nr) || is_blocked(nc, nr))
                continue;
            const int ng = current.g + 1;
            const int ni = index(nc, nr);
            if (best_g[ni] != -1 && best_g[ni] <= ng)
                continue;
            best_g[ni] = ng;
            parent[ni] = {current.col, current.row};
            open.insert(OpenEntry{ng + manhattan(nc, nr, goal_col, goal_row), ng, nc, nr});
        }
    }
    if (!found)
        return waypoints;

    // 回溯路径（从终点到起点），反转输出；跳过起点，保留终点。
    std::vector<std::pair<int, int>> cells;
    int c = goal_col;
    int r = goal_row;
    while (c != start->first || r != start->second) {
        cells.push_back({c, r});
        const auto p = parent[index(c, r)];
        if (p.first < 0)
            return {}; // 不应发生（found=true 保证可达）
        c = p.first;
        r = p.second;
    }
    std::reverse(cells.begin(), cells.end());
    waypoints.reserve(cells.size());
    for (const auto& [wc, wr] : cells)
        waypoints.push_back(cell_to_world(wc, wr));
    return waypoints;
}

} // namespace npc_agent::testing
