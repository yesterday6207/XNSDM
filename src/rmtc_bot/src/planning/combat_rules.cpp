#include "rmtc_bot/planning/combat_rules.hpp"

#include <cmath>

#include "rmtc_bot/events/event_state.hpp"
#include "rmtc_bot/planning/path_planner.hpp"

namespace rmtc_bot {

Cell cell_in_direction(const Cell& origin, const std::string& direction, int distance) {
  for (const auto& item : kDirections) {
    if (direction == item.name) {
      return Cell{origin.first + item.dx * distance, origin.second + item.dy * distance};
    }
  }
  return origin;
}

std::optional<std::pair<EnemyTrack, int>> line_target(const Cell& self_position,
                                                      const std::vector<EnemyTrack>& enemies,
                                                      const std::string& direction,
                                                      int max_range) {
  for (int distance = 1; distance <= max_range; ++distance) {
    const Cell cell = cell_in_direction(self_position, direction, distance);
    for (const auto& enemy : enemies) {
      if (enemy.hp <= 0) {
        continue;
      }
      if (enemy.position == cell) {
        return std::make_pair(enemy, distance);
      }
    }
  }
  return std::nullopt;
}

std::optional<AttackChoice> best_attack(const Cell& self_position,
                                        const std::vector<EnemyTrack>& enemies, int tick,
                                        const EventState* event_state, int max_range) {
  const bool reflect =
      event_state != nullptr && event_state->clean_reflect_active(tick);
  std::optional<AttackChoice> best;
  for (const auto& direction : kDirections) {
    const auto found = line_target(self_position, enemies, direction.name, max_range);
    if (!found.has_value()) {
      continue;
    }
    const EnemyTrack& target = found->first;
    const int distance = found->second;
    if (target.protected_) {
      continue;
    }
    // 洁身自好：攻击防御中且无保护的敌人会反伤且不造成伤害
    if (reflect && target.defending) {
      continue;
    }
    AttackChoice choice;
    choice.direction = direction.name;
    choice.target = target;
    choice.distance = distance;
    if (!best.has_value()) {
      best = choice;
      continue;
    }
    if (choice.target.hp < best->target.hp ||
        (choice.target.hp == best->target.hp && choice.distance < best->distance)) {
      best = choice;
    }
  }
  return best;
}

std::vector<EnemyTrack> enemies_within(const Cell& self_position,
                                       const std::vector<EnemyTrack>& enemies, int radius) {
  std::vector<EnemyTrack> result;
  for (const auto& enemy : enemies) {
    if (enemy.hp <= 0) {
      continue;
    }
    const int dx = std::abs(enemy.position.first - self_position.first);
    const int dy = std::abs(enemy.position.second - self_position.second);
    if (std::max(dx, dy) <= radius) {
      result.push_back(enemy);
    }
  }
  return result;
}

int normal_damage(bool target_defending, bool target_protected, bool vulnerable) {
  if (target_protected) {
    return 0;
  }
  int damage = target_defending ? 10 : 20;
  if (vulnerable) {
    damage *= 2;
  }
  return damage;
}

}  // namespace rmtc_bot
