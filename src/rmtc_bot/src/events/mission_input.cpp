#include "rmtc_bot/events/mission_input.hpp"

namespace rmtc_bot {

MissionInput::MissionInput() { reset(); }

void MissionInput::reset() {
  latest_text.clear();
  missions.clear();
  completed_ids.clear();
}

void MissionInput::update_from_text(const std::string& text) { latest_text = text; }

void MissionInput::update_from_perception(const Perception& perception) {
  latest_text = perception.mission_text;
  missions = perception.missions;
  completed_ids.clear();
  for (const auto& id : perception.score.mission_completed_ids) {
    completed_ids.insert(id);
  }
}

std::set<std::string> MissionInput::incomplete_types() const {
  if (missions.empty()) {
    // 协议公开的标准三项任务
    return {"normal_resources", "high_resources", "reach"};
  }
  std::set<std::string> result;
  for (const auto& mission : missions) {
    if (mission.type.empty()) {
      continue;
    }
    if (completed_ids.count(mission.id) > 0) {
      continue;
    }
    result.insert(mission.type);
  }
  return result;
}

std::optional<Cell> MissionInput::reach_target(const Cell& fallback) const {
  if (missions.empty()) {
    // 只在 mission_text 中给出任务时，仍按公开的标准三项任务执行，使用默认到达点
    return fallback;
  }
  bool reach_pending = false;
  for (const auto& mission : missions) {
    if (mission.type != "reach") {
      continue;
    }
    if (completed_ids.count(mission.id) > 0) {
      continue;
    }
    reach_pending = true;
    if (mission.position.has_value()) {
      return mission.position;
    }
  }
  if (reach_pending) {
    // 任务未给坐标时退化为默认到达点，避免整场不去任务点
    return fallback;
  }
  return std::nullopt;
}

bool MissionInput::needs(const std::string& mission_type) const {
  return incomplete_types().count(mission_type) > 0;
}

}  // namespace rmtc_bot
