#pragma once

#include <optional>
#include <string>

#include "rmtc_bot/events/event_state.hpp"
#include "rmtc_bot/events/mission_input.hpp"
#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/perception/state_store.hpp"
#include "rmtc_bot/planning/grid_map.hpp"
#include "rmtc_bot/types.hpp"
#include "rmtc_bot/utils/config_loader.hpp"

namespace rmtc_bot {

struct Intent {
  std::string action;
  std::optional<std::string> direction;
  std::string reason;

  Intent() = default;
  Intent(std::string action_name, std::optional<std::string> dir, std::string why)
      : action(std::move(action_name)), direction(std::move(dir)), reason(std::move(why)) {}
  explicit Intent(const std::string& action_name) : action(action_name) {}
};

struct DecisionContext {
  DecisionContext(int tick_value, int stage_value, const Perception& perception_value,
                  StateStore& state_value, GridMap& grid_value,
                  EntityTracker& tracker_value, EventState& events_value,
                  MissionInput& missions_value, const PlanningParams& planning_value)
      : tick(tick_value),
        stage(stage_value),
        perception(perception_value),
        state(state_value),
        grid(grid_value),
        tracker(tracker_value),
        events(events_value),
        missions(missions_value),
        planning(planning_value) {}

  int tick;
  int stage;
  const Perception& perception;
  StateStore& state;
  GridMap& grid;
  EntityTracker& tracker;
  EventState& events;
  MissionInput& missions;
  const PlanningParams& planning;
};

}  // namespace rmtc_bot
