#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

/// 任务公告输入与结构化任务状态。
class MissionInput {
 public:
  MissionInput();

  void reset();
  void update_from_text(const std::string& text);
  void update_from_perception(const Perception& perception);

  std::set<std::string> incomplete_types() const;
  std::optional<Cell> reach_target(const Cell& fallback) const;
  bool needs(const std::string& mission_type) const;

  std::string latest_text;
  std::vector<Mission> missions;
  std::set<std::string> completed_ids;
};

}  // namespace rmtc_bot
