#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct EnemyTrack {
  std::string robot_id;
  std::optional<std::string> team;
  Cell position{0, 0};
  double hp = 0.0;
  bool defending = false;
  bool protected_ = false;
  int last_seen_tick = 0;
};

struct ResourceTrack {
  std::string resource_id;
  Cell position{0, 0};
  int value = 0;
  int last_seen_tick = 0;
};

/// 视野内敌人与资源的短期记忆；记忆不是裁判实时真值。
class EntityTracker {
 public:
  explicit EntityTracker(int resource_memory_ticks = 120);

  void reset();
  void update(const Perception& perception);

  std::vector<EnemyTrack> visible_enemies(const Perception& perception) const;
  /// 最近 max_age Tick 内见过的敌人（用于视野外的追击；不是实时真值）。
  std::vector<EnemyTrack> remembered_enemies(int tick, int max_age) const;
  /// 到达记忆位置仍未遇到目标时丢弃该敌人记忆，避免被旧目标反复拉回。
  void forget_enemy(const std::string& robot_id);
  std::unordered_set<Cell, CellHash> enemy_cells(const Perception& perception) const;
  std::vector<ResourceTrack> active_resources(int tick) const;

 private:
  int resource_memory_ticks_;
  std::unordered_map<std::string, EnemyTrack> enemies_;
  std::unordered_map<std::string, ResourceTrack> resources_;
};

}  // namespace rmtc_bot
