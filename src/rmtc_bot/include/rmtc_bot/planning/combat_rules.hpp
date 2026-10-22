#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

class EventState;

struct AttackChoice {
  std::string direction;
  EnemyTrack target;
  int distance = 0;
};

Cell cell_in_direction(const Cell& origin, const std::string& direction, int distance);

/// 该方向直线 1..max_range 内最近的存活敌人及距离；保护目标也会挡住后方目标。
std::optional<std::pair<EnemyTrack, int>> line_target(const Cell& self_position,
                                                      const std::vector<EnemyTrack>& enemies,
                                                      const std::string& direction,
                                                      int max_range = 2);

std::optional<AttackChoice> best_attack(const Cell& self_position,
                                        const std::vector<EnemyTrack>& enemies, int tick,
                                        const EventState* event_state = nullptr,
                                        int max_range = 2);

std::vector<EnemyTrack> enemies_within(const Cell& self_position,
                                       const std::vector<EnemyTrack>& enemies, int radius);

int normal_damage(bool target_defending, bool target_protected, bool vulnerable);

}  // namespace rmtc_bot
