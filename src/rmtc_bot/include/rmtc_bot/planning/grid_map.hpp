#pragma once

#include <string>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct TempWall {
  CellSet cells;
  int start_tick = 0;
  int end_tick = 0;
  std::string kind = "world_split";
};

/// 本地网格、障碍记忆与临时墙推断。
class GridMap {
 public:
  GridMap();

  void reset();
  void mark_temp_blocked(const Cell& cell, int tick, int ttl = 5);
  void set_map(int width, int height);
  void update_from_perception(const Perception& perception);
  void add_temp_wall_cross(int x, int y, int start_tick, int end_tick,
                           const std::string& kind = "world_split");
  void prune(int tick);

  bool in_bounds(const Cell& cell) const;
  bool is_static_obstacle(const Cell& cell) const;
  bool is_temp_wall(const Cell& cell, int tick) const;
  bool is_blocked(const Cell& cell, int tick) const;

  std::vector<Cell> unknown_cells() const;

  int width = 0;
  int height = 0;
  CellSet obstacles;
  /// 障碍最后一次被感知确认的 Tick；用于让临时障碍（如世界分裂十字）随记忆过期。
  CellIntMap obstacle_seen;
  CellSet seen;
  std::vector<TempWall> temp_walls;
  CellIntMap temp_blocked;

 private:
  void mark_seen(const Cell& center, int radius);
};

}  // namespace rmtc_bot
