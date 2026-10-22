#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/planning/path_planner.hpp"
#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct ResourceChoice {
  std::string resource_id;
  Cell position{0, 0};
  int value = 0;
  int distance = 0;
  std::optional<std::string> step;
};

std::optional<ResourceChoice> pick_resource(const Cell& start,
                                            const std::vector<ResourceTrack>& resources,
                                            int width, int height, const BlockedFn& blocked,
                                            const std::string& prefer = "any",
                                            const CellSet& extra_blocked = CellSet(),
                                            bool tour_lookahead = false);

/// 返回 (目标格, 第一步方向)。
std::optional<std::pair<Cell, std::optional<std::string>>> pick_frontier(
    const Cell& start, const CellSet& seen, int width, int height, const BlockedFn& blocked,
    const CellSet& extra_blocked = CellSet());

std::optional<Cell> reach_target_from_missions(const std::vector<Mission>& missions,
                                               const std::set<std::string>& completed_ids,
                                               const Cell& fallback);

}  // namespace rmtc_bot
