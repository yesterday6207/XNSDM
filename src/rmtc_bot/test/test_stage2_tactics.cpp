#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rmtc_bot/decision/safety_guard.hpp"
#include "rmtc_bot/decision/stage2_combat.hpp"
#include "rmtc_bot/events/event_state.hpp"
#include "rmtc_bot/events/mission_input.hpp"
#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/perception/state_store.hpp"
#include "rmtc_bot/planning/grid_map.hpp"
#include "rmtc_bot/utils/config_loader.hpp"

namespace {

using rmtc_bot::Cell;
using rmtc_bot::DecisionContext;
using rmtc_bot::Intent;

struct TacticsFixture {
  rmtc_bot::Perception perception;
  rmtc_bot::StateStore state;
  rmtc_bot::GridMap grid;
  rmtc_bot::EntityTracker tracker;
  rmtc_bot::EventState events;
  rmtc_bot::MissionInput missions;
  rmtc_bot::PlanningParams planning;

  TacticsFixture() {
    perception.tick = 0;
    perception.session_id = "session-1";
    perception.map_width = 25;
    perception.map_height = 25;
    perception.vision_radius = 4;
    perception.self.team = "A";
    perception.self.robot_id = "A";
    perception.self.position = Cell{12, 12};
    perception.self.hp = 100.0;
    perception.self.alive = true;
    grid.set_map(25, 25);
    advance_to(0);
  }

  void advance_to(int tick) {
    perception.tick = tick;
    state.update(perception);
    tracker.update(perception);
    grid.update_from_perception(perception);
    missions.update_from_perception(perception);
  }

  DecisionContext context() {
    return DecisionContext(perception.tick, 2, perception, state, grid, tracker, events,
                           missions, planning);
  }

  void see_everything() {
    for (int x = 0; x < 25; ++x) {
      for (int y = 0; y < 25; ++y) {
        grid.seen.insert(Cell{x, y});
      }
    }
  }

  void add_robot(const std::string& id, const Cell& position, double hp,
                 bool defending = false) {
    rmtc_bot::VisibleEntity entity;
    entity.type = "robot";
    entity.id = id;
    entity.team = "B";
    entity.robot_id = id;
    entity.position = position;
    entity.hp = hp;
    entity.defending = defending;
    perception.entities.push_back(entity);
  }

  void add_resource(const std::string& id, const Cell& position, int value) {
    rmtc_bot::VisibleEntity entity;
    entity.type = value >= 20 ? "resource_high" : "resource_normal";
    entity.id = id;
    entity.position = position;
    entity.value = value;
    perception.entities.push_back(entity);
  }

  void add_obstacle(const Cell& position) {
    rmtc_bot::VisibleEntity entity;
    entity.type = "obstacle";
    entity.position = position;
    perception.entities.push_back(entity);
  }

  void start_fortress(int from_tick, int until_tick) {
    rmtc_bot::TimedEvent event;
    event.type = "duel";
    event.active_from = from_tick;
    event.active_until = until_tick;
    event.area = rmtc_bot::Area{11, 11, 13, 13};
    events.events.push_back(event);
  }

  void hold_fortress(int from_tick, int ticks) {
    for (int offset = 0; offset < ticks; ++offset) {
      advance_to(from_tick + offset);
      const Intent intent = rmtc_bot::stage2_decide(context());
      EXPECT_EQ(intent.action, "defend") << "tick=" << from_tick + offset;
      EXPECT_NE(intent.reason.find("stage2_fortress_hold"), std::string::npos);
    }
  }
};

}  // namespace

TEST(Stage2Tactics, LatePhaseWithoutTargetsKeepsMoving) {
  TacticsFixture fixture;
  fixture.see_everything();
  fixture.advance_to(480);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_NE(intent.reason.find("stage2_patrol"), std::string::npos);
}

TEST(Stage2Tactics, LatePhaseHuntsRememberedEnemyWhenOutOfSight) {
  TacticsFixture fixture;
  fixture.see_everything();
  fixture.add_robot("B", Cell{16, 12}, 100.0);
  fixture.advance_to(380);
  fixture.perception.entities.clear();
  fixture.advance_to(390);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
  EXPECT_NE(intent.reason.find("stage2_hunt_last_seen"), std::string::npos);
}

TEST(Stage2Tactics, EarlyPhaseExploresInsteadOfChasingStaleTarget) {
  TacticsFixture fixture;
  fixture.add_robot("B", Cell{16, 12}, 100.0);
  fixture.advance_to(10);
  fixture.perception.entities.clear();
  fixture.advance_to(20);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_EQ(intent.reason.find("stage2_hunt_last_seen"), std::string::npos);
  EXPECT_NE(intent.reason.find("stage2_explore"), std::string::npos);
}

TEST(Stage2Tactics, EarlyPhaseKeepsCollectingWhenResourcesVisible) {
  TacticsFixture fixture;
  fixture.add_resource("normal1", Cell{14, 12}, 10);
  fixture.advance_to(120);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
  EXPECT_NE(intent.reason.find("stage2_resource:normal1"), std::string::npos);
}

TEST(Stage2Tactics, EarlyPhaseAttacksAdjacentEnemy) {
  TacticsFixture fixture;
  fixture.add_robot("B", Cell{13, 12}, 100.0);
  fixture.advance_to(10);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "attack");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
}

TEST(Stage2Tactics, EarlyPhaseIgnoresDistantEnemyToCollectResource) {
  TacticsFixture fixture;
  fixture.add_robot("B", Cell{14, 12}, 100.0);
  fixture.add_resource("normal1", Cell{10, 15}, 10);
  fixture.advance_to(10);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_resource:normal1"), std::string::npos);
}

TEST(Stage2Tactics, LatePhaseAttacksEnemyInLine) {
  TacticsFixture fixture;
  fixture.add_robot("B", Cell{14, 12}, 100.0);
  fixture.advance_to(400);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "attack");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
  EXPECT_NE(intent.reason.find("stage2_attack"), std::string::npos);
}

TEST(Stage2Tactics, LatePhaseAttacksEvenWhenNotLethal) {
  TacticsFixture fixture;
  fixture.perception.self.hp = 40.0;
  fixture.add_robot("B", Cell{14, 12}, 100.0);
  fixture.advance_to(400);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "attack");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
}

TEST(Stage2Tactics, LowHpTradesWhenAlreadyInRange) {
  // 已在射程内时后撤这 Tick 仍会吃满同一次伤害，先换血更划算
  TacticsFixture fixture;
  fixture.perception.self.hp = 25.0;
  fixture.add_robot("B", Cell{14, 12}, 100.0);
  fixture.advance_to(10);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "attack");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
}

TEST(Stage2Tactics, LowHpDoesNotChaseEnemyOutOfRange) {
  // 低血且敌人尚在射程之外：不主动接敌，避免在视野边缘来回拉扯
  TacticsFixture fixture;
  fixture.see_everything();
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  fixture.perception.missions.push_back(reach);
  fixture.perception.score.mission_completed_ids.push_back("reach_target");
  fixture.perception.self.hp = 35.0;
  fixture.perception.self.position = Cell{6, 9};
  fixture.add_robot("B", Cell{10, 9}, 100.0);
  fixture.advance_to(400);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_EQ(intent.reason.find("stage2_hunt"), std::string::npos);
}

TEST(Stage2Tactics, FortressHoldThenLeavesForCombat) {
  TacticsFixture fixture;
  fixture.start_fortress(10, 60);
  // 第 required 次驻留的 Tick 就会取得增益，因此先验证 required-1 次原地驻留
  fixture.hold_fortress(10, fixture.planning.fortress_stay_ticks - 1);
  fixture.advance_to(10 + fixture.planning.fortress_stay_ticks - 1);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_EQ(intent.reason.find("stage2_fortress_hold"), std::string::npos);
}

TEST(Stage2Tactics, DuelBuffAttackPassesSafetyGuard) {
  TacticsFixture fixture;
  fixture.start_fortress(10, 60);
  fixture.hold_fortress(10, fixture.planning.fortress_stay_ticks - 1);
  fixture.add_robot("B", Cell{14, 14}, 100.0);
  fixture.advance_to(10 + fixture.planning.fortress_stay_ticks - 1);
  auto ctx = fixture.context();
  const Intent intent = rmtc_bot::stage2_decide(ctx);
  EXPECT_EQ(intent.action, "attack");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_NE(intent.reason.find("stage2_duel_attack"), std::string::npos);
  const rmtc_bot::SafetyGuard guard(2);
  EXPECT_EQ(guard.check(intent, ctx).action, "attack");
}

TEST(Stage2Tactics, EarlyPhaseGoesToReachWhenNoResourceVisible) {
  TacticsFixture fixture;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{20, 20};
  fixture.perception.missions.push_back(reach);
  fixture.advance_to(60);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_reach"), std::string::npos);
}

TEST(Stage2Tactics, EarlyPhasePrefersResourceOverReach) {
  TacticsFixture fixture;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{20, 20};
  fixture.perception.missions.push_back(reach);
  fixture.add_resource("normal1", Cell{14, 12}, 10);
  fixture.advance_to(60);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_resource:normal1"), std::string::npos);
}

TEST(Stage2Tactics, ReachMissionTakesPriorityAfterThreshold) {
  TacticsFixture fixture;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{20, 20};
  fixture.perception.missions.push_back(reach);
  fixture.add_resource("normal1", Cell{2, 2}, 10);
  fixture.advance_to(200);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_reach"), std::string::npos);
}

TEST(Stage2Tactics, UnreachableReachTargetStillActs) {
  TacticsFixture fixture;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{20, 20};
  fixture.perception.missions.push_back(reach);
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      fixture.grid.obstacles.insert(Cell{20 + dx, 20 + dy});
    }
  }
  fixture.see_everything();
  fixture.advance_to(200);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_NE(intent.action, "wait");
}

TEST(Stage2Tactics, EmptyMissionsStillUsesDefaultReachTarget) {
  rmtc_bot::MissionInput missions;
  rmtc_bot::Perception perception;
  missions.update_from_perception(perception);
  EXPECT_TRUE(missions.needs("reach"));
  const auto target = missions.reach_target(Cell{7, 7});
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, (Cell(7, 7)));
}

TEST(Stage2Tactics, ReachMissionWithoutPositionUsesDefault) {
  rmtc_bot::MissionInput missions;
  rmtc_bot::Perception perception;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  perception.missions.push_back(reach);
  missions.update_from_perception(perception);
  const auto target = missions.reach_target(Cell{7, 7});
  ASSERT_TRUE(target.has_value());
  EXPECT_EQ(*target, (Cell(7, 7)));
}

TEST(Stage2Tactics, CompletedReachMissionHasNoTarget) {
  rmtc_bot::MissionInput missions;
  rmtc_bot::Perception perception;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  perception.missions.push_back(reach);
  perception.score.mission_completed_ids.push_back("reach_target");
  missions.update_from_perception(perception);
  EXPECT_FALSE(missions.reach_target(Cell{7, 7}).has_value());
  EXPECT_FALSE(missions.needs("reach"));
}

TEST(Stage2Tactics, PrefersHighValueResourceForMission) {
  TacticsFixture fixture;
  rmtc_bot::Mission normal_mission;
  normal_mission.id = "collect_two_normal";
  normal_mission.type = "normal_resources";
  rmtc_bot::Mission high_mission;
  high_mission.id = "collect_high_value";
  high_mission.type = "high_resources";
  fixture.perception.missions.push_back(normal_mission);
  fixture.perception.missions.push_back(high_mission);
  fixture.add_resource("normal1", Cell{11, 12}, 10);
  fixture.add_resource("high1", Cell{14, 12}, 20);
  fixture.advance_to(10);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_EQ(*intent.direction, "right");
  EXPECT_NE(intent.reason.find("stage2_resource:high1"), std::string::npos);
}

TEST(Stage2Tactics, KeepsStaleResourceThatIsStillValid) {
  TacticsFixture fixture;
  // 两处资源都在视野半径 4 之外，避免“视野内未出现即视为被收集”的即时剔除
  fixture.add_resource("near", Cell{12, 6}, 10);
  fixture.add_resource("far", Cell{20, 12}, 10);
  fixture.advance_to(0);
  fixture.perception.entities.clear();
  fixture.advance_to(12);
  // 只重新看到远处资源，近处资源已经 12 Tick 未确认但仍应保留
  fixture.add_resource("far", Cell{20, 12}, 10);
  fixture.advance_to(12);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_resource:near"), std::string::npos);
}

TEST(Stage2Tactics, TemporaryWallDoesNotTrapRobot) {
  TacticsFixture fixture;
  fixture.perception.map_width = 18;
  fixture.perception.map_height = 18;
  fixture.grid.set_map(18, 18);
  fixture.see_everything();
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  fixture.perception.missions.push_back(reach);
  fixture.perception.score.mission_completed_ids.push_back("reach_target");
  fixture.perception.self.position = Cell{4, 4};

  // 世界分裂生成的临时十字墙：横 y=8、竖 x=8，全部在视野半径 4 内
  for (int x = 0; x <= 8; ++x) {
    fixture.add_obstacle(Cell{x, 8});
  }
  for (int y = 0; y <= 8; ++y) {
    fixture.add_obstacle(Cell{8, y});
  }
  fixture.advance_to(130);
  EXPECT_TRUE(fixture.grid.is_blocked(Cell{8, 8}, 130));

  // 十字墙消失且本 Tick 视野内不再出现：记忆障碍必须立即失效，不能把路径封死
  fixture.perception.entities.clear();
  fixture.advance_to(145);
  EXPECT_FALSE(fixture.grid.is_blocked(Cell{8, 8}, 145));
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
}

TEST(Stage2Tactics, PatrolDoesNotReverseLastMove) {
  TacticsFixture fixture;
  fixture.perception.map_width = 18;
  fixture.perception.map_height = 18;
  fixture.grid.set_map(18, 18);
  fixture.see_everything();
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  fixture.perception.missions.push_back(reach);
  fixture.perception.score.mission_completed_ids.push_back("reach_target");
  fixture.perception.self.position = Cell{7, 14};
  // 上一 Tick 刚向下移动，若巡逻立刻向上折返就是相邻两格之间的振荡
  fixture.events.note_move(210, "down");
  fixture.advance_to(211);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  ASSERT_TRUE(intent.direction.has_value());
  EXPECT_NE(*intent.direction, "up");
}

TEST(Stage2Tactics, DefendsWhenFocusedByTwoEnemies) {
  TacticsFixture fixture;
  fixture.perception.self.hp = 50.0;
  fixture.add_robot("B", Cell{14, 12}, 100.0);
  fixture.add_robot("C", Cell{12, 14}, 100.0);
  fixture.advance_to(400);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "defend");
  EXPECT_NE(intent.reason.find("stage2_defend_focus"), std::string::npos);
}

TEST(Stage2Tactics, ForgetsEnemyMemoryAfterReachingItsSpot) {
  TacticsFixture fixture;
  fixture.see_everything();
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  fixture.perception.missions.push_back(reach);
  fixture.perception.score.mission_completed_ids.push_back("reach_target");
  fixture.perception.self.position = Cell{4, 10};
  fixture.add_robot("B", Cell{4, 8}, 100.0);
  fixture.advance_to(380);
  fixture.perception.entities.clear();
  fixture.advance_to(381);
  const Intent approach = rmtc_bot::stage2_decide(fixture.context());
  ASSERT_TRUE(approach.direction.has_value());
  EXPECT_EQ(*approach.direction, "up");
  EXPECT_NE(approach.reason.find("stage2_hunt_last_seen:B"), std::string::npos);

  // 走到记忆位置旁边仍未遇见目标：丢弃记忆，不能下一步又被旧目标拉回去
  fixture.perception.self.position = Cell{4, 9};
  fixture.advance_to(382);
  const Intent arrived = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(arrived.reason.find("stage2_hunt_last_seen:B"), std::string::npos);
  EXPECT_TRUE(fixture.tracker.remembered_enemies(382, 30).empty());
}

TEST(Stage2Tactics, LowHpDoesNotChaseRememberedEnemy) {
  TacticsFixture fixture;
  fixture.see_everything();
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  fixture.perception.missions.push_back(reach);
  fixture.perception.score.mission_completed_ids.push_back("reach_target");
  // 15 HP：再吃一次普通攻击就被击毁，不能主动接敌
  fixture.perception.self.hp = 15.0;
  fixture.perception.self.position = Cell{4, 9};
  fixture.add_robot("B", Cell{8, 9}, 100.0);
  fixture.advance_to(380);
  fixture.perception.entities.clear();
  fixture.advance_to(390);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_EQ(intent.reason.find("stage2_hunt_last_seen"), std::string::npos);
}

TEST(Stage2Tactics, RetreatStaysStickyAfterLeavingVision) {
  TacticsFixture fixture;
  fixture.see_everything();
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{9, 9};
  fixture.perception.missions.push_back(reach);
  fixture.perception.score.mission_completed_ids.push_back("reach_target");
  fixture.perception.self.hp = 40.0;
  fixture.perception.self.position = Cell{14, 12};
  fixture.add_robot("B", Cell{15, 11}, 100.0);
  fixture.advance_to(60);
  const Intent first = rmtc_bot::stage2_decide(fixture.context());
  ASSERT_TRUE(first.direction.has_value());
  const std::string away = *first.direction;
  // 退到视野边缘外：本 Tick 看不到敌人，但记忆仍在，不能立刻往回走
  fixture.perception.entities.clear();
  fixture.perception.self.position = Cell{14, 13};
  fixture.advance_to(61);
  const Intent second = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(second.action, "move");
  ASSERT_TRUE(second.direction.has_value());
  const std::string back = away == "up" ? "down" : away == "down" ? "up"
                          : away == "left" ? "right" : "left";
  EXPECT_NE(*second.direction, back);
}

TEST(Stage2Tactics, DropsResourceWhenEnemyWasBesideIt) {
  TacticsFixture without_enemy;
  without_enemy.add_resource("r", Cell{5, 5}, 10);
  without_enemy.advance_to(0);
  without_enemy.perception.entities.clear();
  without_enemy.advance_to(6);
  const Intent baseline = rmtc_bot::stage2_decide(without_enemy.context());
  EXPECT_NE(baseline.reason.find("stage2_resource:r"), std::string::npos);

  TacticsFixture with_enemy;
  with_enemy.add_resource("r", Cell{5, 5}, 10);
  with_enemy.add_robot("B", Cell{5, 4}, 100.0);
  with_enemy.advance_to(0);
  with_enemy.perception.entities.clear();
  with_enemy.advance_to(6);
  const Intent contested = rmtc_bot::stage2_decide(with_enemy.context());
  EXPECT_EQ(contested.reason.find("stage2_resource:r"), std::string::npos);
  EXPECT_NE(contested.action, "wait");
}

TEST(Stage2Tactics, DefendsWhenHitWouldBeLethal) {
  TacticsFixture fixture;
  fixture.perception.self.hp = 15.0;
  fixture.add_robot("B", Cell{14, 12}, 100.0);
  fixture.advance_to(10);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "defend");
  EXPECT_NE(intent.reason.find("stage2_defend_lethal"), std::string::npos);
}

TEST(Stage2Tactics, DefendsWhenTrappedInEnemyLine) {
  TacticsFixture fixture;
  fixture.perception.self.hp = 40.0;
  // 敌人不在射程内但能看到，且四周被堵：无法换血时用防御把本 Tick 伤害减半
  fixture.add_robot("B", Cell{16, 16}, 100.0);
  fixture.grid.obstacles.insert(Cell{11, 12});
  fixture.grid.obstacles.insert(Cell{12, 11});
  fixture.grid.obstacles.insert(Cell{12, 13});
  fixture.advance_to(10);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_NE(intent.action, "wait");
}

TEST(Stage2Tactics, LlmFightStartsHuntingEarlier) {
  TacticsFixture fixture;
  fixture.see_everything();
  fixture.add_robot("B", Cell{16, 12}, 100.0);
  fixture.advance_to(280);
  fixture.perception.entities.clear();
  fixture.advance_to(300);
  fixture.events.strategy = "fight";
  fixture.events.strategy_tick = 299;
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_hunt_last_seen"), std::string::npos);

  TacticsFixture baseline;
  baseline.see_everything();
  baseline.add_robot("B", Cell{16, 12}, 100.0);
  baseline.advance_to(280);
  baseline.perception.entities.clear();
  baseline.advance_to(300);
  const Intent calm = rmtc_bot::stage2_decide(baseline.context());
  EXPECT_EQ(calm.reason.find("stage2_hunt_last_seen"), std::string::npos);
  EXPECT_NE(calm.action, "wait");
}

TEST(Stage2Tactics, LlmCollectDefersHunting) {
  TacticsFixture fixture;
  fixture.see_everything();
  fixture.events.strategy = "collect";
  fixture.events.strategy_tick = 344;
  fixture.add_robot("B", Cell{16, 12}, 100.0);
  fixture.advance_to(330);
  fixture.perception.entities.clear();
  fixture.advance_to(345);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.reason.find("stage2_hunt_last_seen"), std::string::npos);
  EXPECT_NE(intent.action, "wait");
}

TEST(Stage2Tactics, LlmReachPrefersReachOverCheapResource) {
  TacticsFixture fixture;
  rmtc_bot::Mission reach;
  reach.id = "reach_target";
  reach.type = "reach";
  reach.position = Cell{20, 20};
  fixture.perception.missions.push_back(reach);
  fixture.add_resource("normal1", Cell{14, 12}, 10);
  fixture.events.strategy = "reach";
  fixture.events.strategy_tick = 59;
  fixture.advance_to(60);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.action, "move");
  EXPECT_NE(intent.reason.find("stage2_reach"), std::string::npos);
}

TEST(Stage2Tactics, LlmDefendAvoidsChasingStaleEnemy) {
  TacticsFixture fixture;
  fixture.see_everything();
  fixture.events.strategy = "avoid";
  fixture.events.strategy_tick = 344;
  fixture.add_robot("B", Cell{16, 12}, 100.0);
  fixture.advance_to(330);
  fixture.perception.entities.clear();
  fixture.advance_to(345);
  const Intent intent = rmtc_bot::stage2_decide(fixture.context());
  EXPECT_EQ(intent.reason.find("stage2_hunt_last_seen"), std::string::npos);
  EXPECT_NE(intent.action, "wait");
}
