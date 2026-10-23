#include "rmtc_bot/planning/grid_map.hpp"

#include <algorithm>

namespace rmtc_bot {

namespace {

/// 障碍记忆上限：世界分裂临时十字墙最长存在 30 Tick，
/// 离开视野后按该上限过期，既记住仍在生效的墙，又不会把临时墙当成永久障碍封死路线。
constexpr int kObstacleMemoryTicks = 40;

}  // namespace

GridMap::GridMap() { reset(); }

void GridMap::reset() {
  width = 0;
  height = 0;
  obstacles.clear();
  obstacle_seen.clear();
  seen.clear();
  temp_walls.clear();
  temp_blocked.clear();
}

void GridMap::mark_temp_blocked(const Cell& cell, int tick, int ttl) {
  const int expiry = tick + std::max(1, ttl);
  const auto it = temp_blocked.find(cell);
  if (it == temp_blocked.end() || expiry > it->second) {
    temp_blocked[cell] = expiry;
  }
}

void GridMap::set_map(int new_width, int new_height) {
  if (width != new_width || height != new_height) {
    reset();
    width = new_width;
    height = new_height;
  }
}

void GridMap::update_from_perception(const Perception& perception) {
  set_map(perception.map_width, perception.map_height);
  CellSet observed;
  for (const auto& entity : perception.entities) {
    if (entity.type == "obstacle") {
      obstacles.insert(entity.position);
      obstacle_seen[entity.position] = perception.tick;
      observed.insert(entity.position);
    }
  }
  // 视野内再次确认不到的记忆障碍说明临时墙已消失，立即放弃，避免把自己封死
  const Cell self_pos = perception.self.position;
  const int radius = perception.vision_radius;
  for (auto it = obstacle_seen.begin(); it != obstacle_seen.end();) {
    const int dx = std::abs(it->first.first - self_pos.first);
    const int dy = std::abs(it->first.second - self_pos.second);
    if (observed.count(it->first) == 0 && std::max(dx, dy) <= radius) {
      obstacles.erase(it->first);
      it = obstacle_seen.erase(it);
    } else {
      ++it;
    }
  }
  mark_seen(perception.self.position, perception.vision_radius);
  prune(perception.tick);
}

void GridMap::mark_seen(const Cell& center, int radius) {
  for (int x = center.first - radius; x <= center.first + radius; ++x) {
    for (int y = center.second - radius; y <= center.second + radius; ++y) {
      const Cell cell{x, y};
      if (in_bounds(cell)) {
        seen.insert(cell);
      }
    }
  }
}

void GridMap::add_temp_wall_cross(int x, int y, int start_tick, int end_tick,
                                  const std::string& kind) {
  TempWall wall;
  wall.start_tick = start_tick;
  wall.end_tick = end_tick;
  wall.kind = kind;
  for (int i = 0; i < width; ++i) {
    wall.cells.insert(Cell{i, y});
  }
  for (int j = 0; j < height; ++j) {
    wall.cells.insert(Cell{x, j});
  }
  temp_walls.push_back(std::move(wall));
}

void GridMap::prune(int tick) {
  temp_walls.erase(std::remove_if(temp_walls.begin(), temp_walls.end(),
                                  [tick](const TempWall& wall) {
                                    return wall.end_tick <= tick;
                                  }),
                   temp_walls.end());
  for (auto it = temp_blocked.begin(); it != temp_blocked.end();) {
    if (it->second <= tick) {
      it = temp_blocked.erase(it);
    } else {
      ++it;
    }
  }
  // 超出记忆窗口且未再被确认的障碍不再视为阻挡
  for (auto it = obstacle_seen.begin(); it != obstacle_seen.end();) {
    if (tick - it->second > kObstacleMemoryTicks) {
      obstacles.erase(it->first);
      it = obstacle_seen.erase(it);
    } else {
      ++it;
    }
  }
}

bool GridMap::in_bounds(const Cell& cell) const {
  return cell.first >= 0 && cell.first < width && cell.second >= 0 && cell.second < height;
}

bool GridMap::is_static_obstacle(const Cell& cell) const {
  return obstacles.count(cell) > 0;
}

bool GridMap::is_temp_wall(const Cell& cell, int tick) const {
  for (const auto& wall : temp_walls) {
    if (wall.start_tick <= tick && tick < wall.end_tick && wall.cells.count(cell) > 0) {
      return true;
    }
  }
  return false;
}

bool GridMap::is_blocked(const Cell& cell, int tick) const {
  if (!in_bounds(cell)) {
    return true;
  }
  if (obstacles.count(cell) > 0) {
    const auto seen = obstacle_seen.find(cell);
    // 没有观察时间的外部注入障碍视为永久；有记录的障碍只在记忆窗口内阻挡
    if (seen == obstacle_seen.end() || tick - seen->second <= kObstacleMemoryTicks) {
      return true;
    }
  }
  const auto blocked = temp_blocked.find(cell);
  if (blocked != temp_blocked.end() && blocked->second > tick) {
    return true;
  }
  return is_temp_wall(cell, tick);
}

std::vector<Cell> GridMap::unknown_cells() const {
  std::vector<Cell> result;
  for (int x = 0; x < width; ++x) {
    for (int y = 0; y < height; ++y) {
      const Cell cell{x, y};
      if (seen.count(cell) == 0 && obstacles.count(cell) == 0) {
        result.push_back(cell);
      }
    }
  }
  return result;
}

}  // namespace rmtc_bot
