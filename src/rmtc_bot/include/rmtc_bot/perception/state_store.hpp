#pragma once

#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

/// 本车状态、场次、地图尺寸与比分存储。
class StateStore {
 public:
  StateStore();

  void reset();
  void begin_session(const std::string& session_id);
  void update(const Perception& perception);

  bool has_seen_tick(int tick) const;
  std::set<std::string> mission_completed_ids() const;

  bool can_move() const;
  bool can_attack(int stage) const;
  bool can_defend(int stage) const;
  bool must_wait() const;

  std::string summary() const;

  std::string session_id;
  int tick = -1;
  int map_width = 0;
  int map_height = 0;
  Cell position{0, 0};
  double hp = 0.0;
  bool alive = false;
  bool defending = false;
  bool protected_ = false;
  int respawn_in = 0;
  std::string team;
  std::string robot_id;
  Score score;
  std::string mission_text;
  std::vector<Mission> missions;
  std::string event_text;
  int vision_radius = 4;
  std::optional<std::string> last_position_reason;

 private:
  std::unordered_set<int> seen_ticks_;
};

}  // namespace rmtc_bot
