#include "rmtc_bot/decision/stage1_collect.hpp"

#include "rmtc_bot/planning/path_planner.hpp"
#include "rmtc_bot/planning/target_selector.hpp"

namespace rmtc_bot {

Intent stage1_decide(const DecisionContext& ctx) {
  const StateStore& state = ctx.state;
  if (!state.alive) {
    return Intent("wait", std::nullopt, "stage1_dead");
  }

  const int tick = ctx.tick;
  const Cell start = state.position;

  const BlockedFn blocked = [&ctx, tick](const Cell& cell) {
    if (ctx.grid.is_blocked(cell, tick)) {
      return true;
    }
    return ctx.events.is_cell_blocked(cell, tick);
  };

  const auto resources = ctx.tracker.active_resources(tick);
  const auto choice = pick_resource(start, resources, state.map_width, state.map_height,
                                    blocked, "any", CellSet(), true);
  if (choice.has_value()) {
    if (choice->step.has_value()) {
      return Intent("move", choice->step, "stage1_resource:" + choice->resource_id);
    }
    return Intent("wait", std::nullopt, "stage1_resource_same_cell");
  }

  if (ctx.planning.exploration_enabled) {
    const auto frontier = pick_frontier(start, ctx.grid.seen, state.map_width, state.map_height,
                                        blocked);
    if (frontier.has_value() && frontier->second.has_value()) {
      return Intent("move", frontier->second, "stage1_explore");
    }
  }

  // 已知资源均不可达时，尝试向地图中心靠拢以改变视野
  const Cell center{state.map_width / 2, state.map_height / 2};
  if (center != start) {
    const auto step = bfs_first_step(start, center, state.map_width, state.map_height, blocked);
    if (step.has_value()) {
      return Intent("move", step, "stage1_center");
    }
  }

  return Intent("wait", std::nullopt, "stage1_no_target");
}

}  // namespace rmtc_bot
