#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct Direction {
  const char* name;
  int dx;
  int dy;
};

extern const std::array<Direction, 4> kDirections;

std::string opposite_direction(const std::string& direction);

Cell move_cell(const Cell& cell, const std::string& direction);

using BlockedFn = std::function<bool(const Cell&)>;

/// BFS 距离图；未知格默认可通行。
CellIntMap bfs_distances(const Cell& start, int width, int height, const BlockedFn& blocked,
                         const CellSet& extra_blocked = CellSet(),
                         int max_expansions = 20000);

/// 返回到达 goal 的第一步方向；不可达或已到达返回 std::nullopt。
std::optional<std::string> bfs_first_step(const Cell& start, const Cell& goal, int width,
                                          int height, const BlockedFn& blocked,
                                          const CellSet& extra_blocked = CellSet(),
                                          int max_expansions = 20000);

}  // namespace rmtc_bot
