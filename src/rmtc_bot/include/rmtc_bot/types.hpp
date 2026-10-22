#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rmtc_bot {

/// 地图坐标，x 向右、y 向下。
using Cell = std::pair<int, int>;

struct CellHash {
  std::size_t operator()(const Cell& cell) const noexcept {
    const auto x = static_cast<std::size_t>(static_cast<unsigned int>(cell.first));
    const auto y = static_cast<std::size_t>(static_cast<unsigned int>(cell.second));
    return (x << 32) ^ y;
  }
};

inline std::ostream& operator<<(std::ostream& os, const Cell& cell) {
  return os << "(" << cell.first << "," << cell.second << ")";
}

using CellSet = std::unordered_set<Cell, CellHash>;
using CellIntMap = std::unordered_map<Cell, int, CellHash>;

std::string cell_to_string(const Cell& cell);

struct LastAction {
  int tick = 0;
  std::string action;
  bool moved = false;
  std::optional<std::string> effective_direction;
  std::optional<std::string> error;
};

struct SelfState {
  std::string team;
  std::string robot_id;
  Cell position{0, 0};
  double hp = 0.0;
  bool alive = false;
  bool defending = false;
  bool protected_ = false;
  int respawn_in = 0;
  std::optional<std::string> position_reason;
  std::optional<LastAction> last_action;
};

struct VisibleEntity {
  std::string type;
  std::string id;
  Cell position{0, 0};
  std::optional<std::string> team;
  std::optional<std::string> robot_id;
  std::optional<double> hp;
  std::optional<int> value;
  bool defending = false;
  bool protected_ = false;
};

struct Score {
  double resource_value = 0.0;
  double effective_damage = 0.0;
  double mission_completed = 0.0;
  std::vector<std::string> mission_completed_ids;
  double invalid_actions = 0.0;
};

struct Mission {
  std::string id;
  std::string type;
  std::optional<double> target;
  std::optional<Cell> position;
};

struct Perception {
  int tick = 0;
  std::string session_id;
  int vision_radius = 4;
  SelfState self;
  std::vector<VisibleEntity> entities;
  Score score;
  std::string mission_text;
  std::vector<Mission> missions;
  int map_width = 0;
  int map_height = 0;
  std::string event_text;
};

}  // namespace rmtc_bot
