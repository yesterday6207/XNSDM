#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "rmtc_bot/ai/output_validator.hpp"
#include "rmtc_bot/events/event_state.hpp"
#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/planning/combat_rules.hpp"
#include "rmtc_bot/planning/grid_map.hpp"
#include "rmtc_bot/planning/path_planner.hpp"
#include "rmtc_bot/planning/target_selector.hpp"

namespace {

using rmtc_bot::Cell;

rmtc_bot::BlockedFn grid_blocked(const rmtc_bot::GridMap& grid, int tick) {
  return [&grid, tick](const Cell& cell) { return grid.is_blocked(cell, tick); };
}

rmtc_bot::EnemyTrack enemy(const std::string& id, Cell position, double hp,
                           bool defending = false, bool protected_flag = false) {
  rmtc_bot::EnemyTrack track;
  track.robot_id = id;
  track.position = position;
  track.hp = hp;
  track.defending = defending;
  track.protected_ = protected_flag;
  track.last_seen_tick = 1;
  return track;
}

}  // namespace

TEST(PathPlanner, BfsDistanceAndFirstStep) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  const auto blocked = grid_blocked(grid, 1);
  const auto distances = rmtc_bot::bfs_distances(Cell{0, 0}, 10, 10, blocked);
  ASSERT_EQ(distances.size(), 100u);
  EXPECT_EQ(distances.at(Cell{0, 0}), 0);
  EXPECT_EQ(distances.at(Cell{3, 0}), 3);
  const auto step = rmtc_bot::bfs_first_step(Cell{0, 0}, Cell{3, 0}, 10, 10, blocked);
  ASSERT_TRUE(step.has_value());
  EXPECT_TRUE(*step == "up" || *step == "down" || *step == "left" || *step == "right");
  EXPECT_EQ(rmtc_bot::move_cell(Cell{0, 0}, *step), (Cell{1, 0}));
}

TEST(PathPlanner, UnreachableGoalReturnsNullopt) {
  rmtc_bot::GridMap grid;
  grid.set_map(5, 5);
  for (int y = 0; y < 5; ++y) {
    grid.obstacles.insert(Cell{2, y});
  }
  const auto blocked = grid_blocked(grid, 1);
  EXPECT_FALSE(rmtc_bot::bfs_first_step(Cell{0, 0}, Cell{4, 4}, 5, 5, blocked).has_value());
}

TEST(PathPlanner, ExtraBlockedRespectedButGoalExempt) {
  rmtc_bot::GridMap grid;
  grid.set_map(5, 5);
  const auto blocked = grid_blocked(grid, 1);
  rmtc_bot::CellSet extra;
  extra.insert(Cell{1, 0});
  const auto step = rmtc_bot::bfs_first_step(Cell{0, 0}, Cell{2, 0}, 5, 5, blocked, extra);
  ASSERT_TRUE(step.has_value());
  EXPECT_EQ(*step, "down");
  EXPECT_TRUE(rmtc_bot::bfs_first_step(Cell{0, 0}, Cell{1, 0}, 5, 5, blocked, extra)
                  .has_value());
}

TEST(GridMap, ObstaclesAndSeenArea) {
  rmtc_bot::Perception perception;
  perception.tick = 3;
  perception.vision_radius = 2;
  perception.map_width = 10;
  perception.map_height = 10;
  perception.self.position = Cell{5, 5};
  rmtc_bot::VisibleEntity obstacle;
  obstacle.type = "obstacle";
  obstacle.position = Cell{6, 5};
  perception.entities.push_back(obstacle);

  rmtc_bot::GridMap grid;
  grid.update_from_perception(perception);
  EXPECT_TRUE(grid.is_blocked(Cell{6, 5}, 3));
  EXPECT_TRUE(grid.is_static_obstacle(Cell{6, 5}));
  EXPECT_TRUE(grid.seen.count(Cell{5, 5}) > 0);
  EXPECT_TRUE(grid.seen.count(Cell{3, 3}) > 0);
  EXPECT_TRUE(grid.seen.count(Cell{0, 0}) == 0);
  EXPECT_TRUE(grid.is_blocked(Cell{-1, 0}, 3));
}

TEST(GridMap, ObstacleRemovedWhenNotSeenInVision) {
  rmtc_bot::Perception perception;
  perception.tick = 100;
  perception.vision_radius = 4;
  perception.map_width = 10;
  perception.map_height = 10;
  perception.self.position = Cell{5, 5};
  rmtc_bot::VisibleEntity obstacle;
  obstacle.type = "obstacle";
  obstacle.position = Cell{6, 5};
  perception.entities.push_back(obstacle);

  rmtc_bot::GridMap grid;
  grid.update_from_perception(perception);
  EXPECT_TRUE(grid.is_blocked(Cell{6, 5}, 100));

  // 同一格仍在视野内却不再被确认：临时墙已消失，必须立刻放行
  perception.tick = 110;
  perception.entities.clear();
  grid.update_from_perception(perception);
  EXPECT_FALSE(grid.is_blocked(Cell{6, 5}, 110));
  EXPECT_TRUE(grid.obstacles.count(Cell{6, 5}) == 0);
}

TEST(GridMap, StaleObstacleExpiresOutsideVision) {
  rmtc_bot::Perception perception;
  perception.tick = 100;
  perception.vision_radius = 4;
  perception.map_width = 10;
  perception.map_height = 10;
  perception.self.position = Cell{5, 5};
  rmtc_bot::VisibleEntity obstacle;
  obstacle.type = "obstacle";
  obstacle.position = Cell{0, 0};
  perception.entities.push_back(obstacle);

  rmtc_bot::GridMap grid;
  grid.update_from_perception(perception);
  EXPECT_TRUE(grid.is_blocked(Cell{0, 0}, 100));

  // 视野外保留记忆（覆盖世界分裂 30 Tick 的临时十字墙），但超过记忆窗口后不再阻挡
  perception.tick = 115;
  perception.entities.clear();
  grid.update_from_perception(perception);
  EXPECT_TRUE(grid.is_blocked(Cell{0, 0}, 115));

  perception.tick = 135;
  grid.update_from_perception(perception);
  EXPECT_TRUE(grid.is_blocked(Cell{0, 0}, 135));

  perception.tick = 146;
  grid.update_from_perception(perception);
  EXPECT_FALSE(grid.is_blocked(Cell{0, 0}, 146));
}

TEST(GridMap, TempBlockedExpires) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  grid.mark_temp_blocked(Cell{4, 4}, 10, 5);
  EXPECT_TRUE(grid.is_blocked(Cell{4, 4}, 10));
  EXPECT_TRUE(grid.is_blocked(Cell{4, 4}, 14));
  EXPECT_FALSE(grid.is_blocked(Cell{4, 4}, 15));
  grid.prune(15);
  EXPECT_TRUE(grid.temp_blocked.empty());
}

TEST(GridMap, WorldSplitTempWall) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  grid.add_temp_wall_cross(4, 4, 10, 40);
  EXPECT_TRUE(grid.is_temp_wall(Cell{4, 0}, 10));
  EXPECT_TRUE(grid.is_temp_wall(Cell{9, 4}, 10));
  EXPECT_FALSE(grid.is_temp_wall(Cell{5, 5}, 10));
  EXPECT_FALSE(grid.is_temp_wall(Cell{4, 0}, 9));
  EXPECT_TRUE(grid.is_blocked(Cell{4, 7}, 10));
}

TEST(TargetSelector, PicksNearestResourceWithStep) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  const auto blocked = grid_blocked(grid, 1);
  std::vector<rmtc_bot::ResourceTrack> resources;
  rmtc_bot::ResourceTrack near;
  near.resource_id = "near";
  near.position = Cell{5, 7};
  near.value = 10;
  near.last_seen_tick = 1;
  resources.push_back(near);
  rmtc_bot::ResourceTrack far;
  far.resource_id = "far";
  far.position = Cell{9, 5};
  far.value = 20;
  far.last_seen_tick = 1;
  resources.push_back(far);

  const auto choice = rmtc_bot::pick_resource(Cell{5, 5}, resources, 10, 10, blocked, "any");
  ASSERT_TRUE(choice.has_value());
  EXPECT_EQ(choice->resource_id, "near");
  ASSERT_TRUE(choice->step.has_value());
  EXPECT_EQ(*choice->step, "down");
}

TEST(TargetSelector, PreferHighFallsBackToAny) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  const auto blocked = grid_blocked(grid, 1);
  std::vector<rmtc_bot::ResourceTrack> resources;
  rmtc_bot::ResourceTrack normal;
  normal.resource_id = "normal";
  normal.position = Cell{6, 5};
  normal.value = 10;
  normal.last_seen_tick = 1;
  resources.push_back(normal);

  const auto choice = rmtc_bot::pick_resource(Cell{5, 5}, resources, 10, 10, blocked, "high");
  ASSERT_TRUE(choice.has_value());
  EXPECT_EQ(choice->resource_id, "normal");

  rmtc_bot::ResourceTrack high;
  high.resource_id = "high";
  high.position = Cell{7, 5};
  high.value = 20;
  high.last_seen_tick = 1;
  resources.push_back(high);
  const auto high_choice =
      rmtc_bot::pick_resource(Cell{5, 5}, resources, 10, 10, blocked, "high");
  ASSERT_TRUE(high_choice.has_value());
  EXPECT_EQ(high_choice->resource_id, "high");
}

TEST(TargetSelector, FrontierChoosesNearestUnknown) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  const auto blocked = grid_blocked(grid, 1);
  rmtc_bot::CellSet seen;
  for (int x = 0; x < 10; ++x) {
    for (int y = 0; y < 10; ++y) {
      if (!(x == 9 && y == 5)) {
        seen.insert(Cell{x, y});
      }
    }
  }
  const auto frontier = rmtc_bot::pick_frontier(Cell{5, 5}, seen, 10, 10, blocked);
  ASSERT_TRUE(frontier.has_value());
  EXPECT_EQ(frontier->first, (Cell{9, 5}));
  ASSERT_TRUE(frontier->second.has_value());
  EXPECT_EQ(*frontier->second, "right");
}

TEST(TargetSelector, TourLookaheadChoosesNearestEndpoint) {
  rmtc_bot::GridMap grid;
  grid.set_map(10, 10);
  const auto blocked = grid_blocked(grid, 1);
  std::vector<rmtc_bot::ResourceTrack> resources;
  for (int index = 0; index < 3; ++index) {
    rmtc_bot::ResourceTrack track;
    track.resource_id = "r" + std::to_string(index);
    track.position = Cell{5 + 2 * index, 5};
    track.value = 10;
    track.last_seen_tick = 1;
    resources.push_back(track);
  }

  const auto choice =
      rmtc_bot::pick_resource(Cell{2, 5}, resources, 10, 10, blocked, "any", rmtc_bot::CellSet(),
                              true);
  ASSERT_TRUE(choice.has_value());
  EXPECT_EQ(choice->resource_id, "r0");
  ASSERT_TRUE(choice->step.has_value());
  EXPECT_EQ(*choice->step, "right");
}

TEST(CombatRules, LineTargetReturnsNearest) {
  std::vector<rmtc_bot::EnemyTrack> enemies{enemy("a", Cell{2, 5}, 20.0),
                                            enemy("b", Cell{1, 5}, 20.0)};
  const auto target = rmtc_bot::line_target(Cell{0, 5}, enemies, "right", 2);
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(target->first.robot_id, "b");
  EXPECT_EQ(target->second, 1);
  EXPECT_FALSE(rmtc_bot::line_target(Cell{0, 5}, enemies, "left", 2).has_value());
}

TEST(CombatRules, ProtectedTargetBlocksLine) {
  std::vector<rmtc_bot::EnemyTrack> enemies{
      enemy("front", Cell{1, 5}, 20.0, false, true), enemy("back", Cell{2, 5}, 10.0)};
  const auto target = rmtc_bot::line_target(Cell{0, 5}, enemies, "right", 2);
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(target->first.robot_id, "front");
  EXPECT_FALSE(rmtc_bot::best_attack(Cell{0, 5}, enemies, 1).has_value());
}

TEST(CombatRules, BestAttackPrefersLowestHp) {
  std::vector<rmtc_bot::EnemyTrack> enemies{
      enemy("right", Cell{1, 5}, 30.0), enemy("down", Cell{0, 6}, 10.0)};
  const auto attack = rmtc_bot::best_attack(Cell{0, 5}, enemies, 1, nullptr, 2);
  ASSERT_TRUE(attack.has_value());
  EXPECT_EQ(attack->direction, "down");
  EXPECT_EQ(attack->target.robot_id, "down");
}

TEST(CombatRules, CleanReflectSkipsDefendingTarget) {
  rmtc_bot::LLMDecision decision;
  decision.strategy = "fight";
  rmtc_bot::LLMEvent clean;
  clean.type = "clean";
  clean.active_from = 1;
  clean.active_until = 20;
  clean.recommendation = "ignore";
  decision.events.push_back(clean);
  rmtc_bot::EventState events;
  events.apply(decision, 1, nullptr);

  std::vector<rmtc_bot::EnemyTrack> enemies{
      enemy("defending", Cell{1, 5}, 50.0, true), enemy("open", Cell{0, 6}, 40.0)};
  const auto attack = rmtc_bot::best_attack(Cell{0, 5}, enemies, 2, &events, 2);
  ASSERT_TRUE(attack.has_value());
  EXPECT_EQ(attack->target.robot_id, "open");
}

TEST(CombatRules, EnemiesWithinUsesChebyshevRadius) {
  std::vector<rmtc_bot::EnemyTrack> enemies{
      enemy("near", Cell{2, 7}, 20.0), enemy("far", Cell{4, 5}, 20.0),
      enemy("dead", Cell{1, 5}, 0.0)};
  const auto result = rmtc_bot::enemies_within(Cell{0, 5}, enemies, 2);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].robot_id, "near");
}

TEST(CombatRules, NormalDamageModifiers) {
  EXPECT_EQ(rmtc_bot::normal_damage(false, false, false), 20);
  EXPECT_EQ(rmtc_bot::normal_damage(true, false, false), 10);
  EXPECT_EQ(rmtc_bot::normal_damage(false, false, true), 40);
  EXPECT_EQ(rmtc_bot::normal_damage(true, true, true), 0);
}

TEST(PathPlanner, DirectionHelpers) {
  EXPECT_EQ(rmtc_bot::opposite_direction("up"), "down");
  EXPECT_EQ(rmtc_bot::opposite_direction("left"), "right");
  EXPECT_EQ(rmtc_bot::move_cell(Cell{3, 3}, "right"), (Cell{4, 3}));
  EXPECT_EQ(rmtc_bot::cell_in_direction(Cell{3, 3}, "down", 2), (Cell{3, 5}));
}
