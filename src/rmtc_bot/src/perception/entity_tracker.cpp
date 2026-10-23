#include "rmtc_bot/perception/entity_tracker.hpp"

#include <cstdlib>

namespace rmtc_bot {

EntityTracker::EntityTracker(int resource_memory_ticks)
    : resource_memory_ticks_(resource_memory_ticks) {
  reset();
}

void EntityTracker::reset() {
  enemies_.clear();
  resources_.clear();
}

void EntityTracker::update(const Perception& perception) {
  const Cell self_pos = perception.self.position;
  std::unordered_set<std::string> seen_resource_ids;

  for (const auto& entity : perception.entities) {
    if (entity.type == "robot" && entity.robot_id.has_value() && !entity.robot_id->empty()) {
      EnemyTrack track;
      track.robot_id = *entity.robot_id;
      track.team = entity.team;
      track.position = entity.position;
      track.hp = entity.hp.value_or(0.0);
      track.defending = entity.defending;
      track.protected_ = entity.protected_;
      track.last_seen_tick = perception.tick;
      const auto previous = enemies_.find(track.robot_id);
      if (previous != enemies_.end()) {
        // 位置变化即视为该敌人本 Tick 成功移动；未见位移时沿用上次记录
        track.last_move_tick = previous->second.position != track.position
                                   ? perception.tick
                                   : previous->second.last_move_tick;
      }
      enemies_[track.robot_id] = track;
    } else if (entity.type == "resource_normal" || entity.type == "resource_high") {
      seen_resource_ids.insert(entity.id);
      ResourceTrack track;
      track.resource_id = entity.id;
      track.position = entity.position;
      if (entity.value.has_value()) {
        track.value = *entity.value;
      } else {
        track.value = entity.type == "resource_high" ? 20 : 10;
      }
      track.last_seen_tick = perception.tick;
      resources_[entity.id] = track;
    }
  }

  // 本次视野覆盖的格子内未出现的资源视为已被收集
  const int radius = perception.vision_radius;
  for (auto it = resources_.begin(); it != resources_.end();) {
    const int dx = std::abs(it->second.position.first - self_pos.first);
    const int dy = std::abs(it->second.position.second - self_pos.second);
    if (std::max(dx, dy) <= radius && seen_resource_ids.count(it->first) == 0) {
      it = resources_.erase(it);
    } else {
      ++it;
    }
  }
}

std::vector<EnemyTrack> EntityTracker::visible_enemies(const Perception& perception) const {
  std::vector<EnemyTrack> result;
  for (const auto& entity : perception.entities) {
    if (entity.type != "robot" || !entity.robot_id.has_value() || entity.robot_id->empty()) {
      continue;
    }
    if (entity.hp.has_value() && *entity.hp <= 0) {
      continue;
    }
    EnemyTrack track;
    track.robot_id = *entity.robot_id;
    track.team = entity.team;
    track.position = entity.position;
    track.hp = entity.hp.value_or(0.0);
    track.defending = entity.defending;
    track.protected_ = entity.protected_;
    track.last_seen_tick = perception.tick;
    // 补上记忆中的位移 Tick，供易伤判断使用
    const auto tracked = enemies_.find(track.robot_id);
    if (tracked != enemies_.end()) {
      track.last_move_tick = tracked->second.last_move_tick;
    }
    result.push_back(track);
  }
  return result;
}

std::vector<EnemyTrack> EntityTracker::remembered_enemies(int tick, int max_age) const {
  std::vector<EnemyTrack> result;
  for (const auto& item : enemies_) {
    if (item.second.hp <= 0) {
      continue;
    }
    if (tick - item.second.last_seen_tick > max_age) {
      continue;
    }
    result.push_back(item.second);
  }
  return result;
}

void EntityTracker::forget_enemy(const std::string& robot_id) {
  enemies_.erase(robot_id);
}

std::unordered_set<Cell, CellHash> EntityTracker::enemy_cells(
    const Perception& perception) const {
  std::unordered_set<Cell, CellHash> cells;
  for (const auto& enemy : visible_enemies(perception)) {
    cells.insert(enemy.position);
  }
  return cells;
}

std::vector<ResourceTrack> EntityTracker::active_resources(int tick) const {
  std::vector<ResourceTrack> result;
  for (const auto& item : resources_) {
    if (tick - item.second.last_seen_tick <= resource_memory_ticks_) {
      result.push_back(item.second);
    }
  }
  return result;
}

}  // namespace rmtc_bot
