#include <gtest/gtest.h>

#include <string>

#include "rmtc_bot/perception/parser.hpp"
#include "rmtc_bot/perception/relative_map.hpp"

namespace {

using rmtc_bot::Cell;

std::string make_grid(int dx, int dy) {
  std::string text;
  for (int row = 0; row < 9; ++row) {
    for (int col = 0; col < 9; ++col) {
      char ch = '0';
      if (row == 4 && col == 4) {
        ch = '1';
      }
      if (row == 4 + dy && col == 4 + dx) {
        ch = '2';
      }
      text.push_back(ch);
    }
    if (row < 8) {
      text.push_back('\n');
    }
  }
  return text;
}

std::string escaped(const std::string& text) {
  std::string out;
  for (const char ch : text) {
    if (ch == '\n') {
      out += "\\n";
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::string complete_perception() {
  const std::string entity_robot = escaped(make_grid(1, 0));
  const std::string entity_resource = escaped(make_grid(0, 2));
  const std::string entity_obstacle = escaped(make_grid(-1, 0));
  return std::string(R"json({"tick":5,"session_id":"s1","vision_radius":3,)json") +
         R"json("self":{"team":"A","robot_id":"r1","position":[10,10],"hp":100,)json" +
         R"json("alive":true,"defending":false,"protected":false,"respawn_in":0,)json" +
         R"json("position_reason":null,"last_action":{"tick":4,"action":"move",)json" +
         R"json("moved":true,"effective_direction":"right","error":null}},)json" +
         R"json("visible_entities":[{"type":"robot","robot_id":"r2","team":"B",)json" +
         R"json("hp":50,"defending":true,"protected":false,"position":")json" + entity_robot +
         R"json("},{"type":"resource_high","id":"res1","value":20,"position":")json" +
         entity_resource + R"json("},{"type":"obstacle","position":")json" + entity_obstacle +
         R"json("},{"type":"unknown_type","id":"x","position":")json" + entity_robot +
         R"json("}],"map":{"width":20,"height":20},"missions":[{"id":"m1",)json" +
         R"json("type":"reach","target":1,"position":[12,12]}],"score":{)json" +
         R"json("resource_value":30,"effective_damage":0,"mission_completed":0,)json" +
         R"json("mission_completed_ids":[],"invalid_actions":1},"mission_text":"tasks",)json" +
         R"json("event_text":"boom"})json";
}

}  // namespace

TEST(PerceptionParse, CompletePerception) {
  const auto result = rmtc_bot::parse_perception(complete_perception());
  ASSERT_TRUE(result.ok) << result.error;
  const auto& perception = result.perception;
  EXPECT_EQ(perception.tick, 5);
  EXPECT_EQ(perception.session_id, "s1");
  EXPECT_EQ(perception.vision_radius, 3);
  EXPECT_EQ(perception.self.team, "A");
  EXPECT_EQ(perception.self.robot_id, "r1");
  EXPECT_EQ(perception.self.position, (Cell{10, 10}));
  EXPECT_DOUBLE_EQ(perception.self.hp, 100.0);
  EXPECT_TRUE(perception.self.alive);
  EXPECT_EQ(perception.map_width, 20);
  EXPECT_EQ(perception.map_height, 20);
  EXPECT_EQ(perception.mission_text, "tasks");
  EXPECT_EQ(perception.event_text, "boom");
  EXPECT_DOUBLE_EQ(perception.score.resource_value, 30.0);
  EXPECT_DOUBLE_EQ(perception.score.invalid_actions, 1.0);
  ASSERT_EQ(perception.missions.size(), 1u);
  EXPECT_EQ(perception.missions[0].id, "m1");
  ASSERT_TRUE(perception.missions[0].position.has_value());
    EXPECT_EQ(*perception.missions[0].position, (Cell{12, 12}));
}

TEST(PerceptionParse, EntitiesResolvedAndBadEntitySkipped) {
  const auto result = rmtc_bot::parse_perception(complete_perception());
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(result.perception.entities.size(), 3u);
  EXPECT_EQ(result.perception.entities[0].type, "robot");
  EXPECT_EQ(result.perception.entities[0].position, (Cell{11, 10}));
  EXPECT_EQ(result.perception.entities[1].type, "resource_high");
  EXPECT_EQ(result.perception.entities[1].position, (Cell{10, 12}));
  EXPECT_EQ(result.perception.entities[2].type, "obstacle");
  EXPECT_EQ(result.perception.entities[2].position, (Cell{9, 10}));
}

TEST(PerceptionParse, LastActionParsed) {
  const auto result = rmtc_bot::parse_perception(complete_perception());
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.perception.self.last_action.has_value());
  EXPECT_EQ(result.perception.self.last_action->tick, 4);
  EXPECT_TRUE(result.perception.self.last_action->moved);
  ASSERT_TRUE(result.perception.self.last_action->effective_direction.has_value());
  EXPECT_EQ(*result.perception.self.last_action->effective_direction, "right");
}

TEST(PerceptionParse, InvalidJsonRejected) {
  const auto result = rmtc_bot::parse_perception("{not json");
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
}

TEST(PerceptionParse, MissingSelfRejected) {
  const auto result = rmtc_bot::parse_perception(
      R"({"tick":1,"session_id":"s","map":{"width":10,"height":10}})");
  EXPECT_FALSE(result.ok);
}

TEST(PerceptionParse, NegativeTickRejected) {
  std::string text = complete_perception();
  const std::string from = "\"tick\":5";
  text.replace(text.find(from), from.size(), "\"tick\":-1");
  const auto result = rmtc_bot::parse_perception(text);
  EXPECT_FALSE(result.ok);
}

TEST(PerceptionParse, SessionIdTypeRejected) {
  std::string text = complete_perception();
  const std::string from = "\"session_id\":\"s1\"";
  text.replace(text.find(from), from.size(), "\"session_id\":1");
  const auto result = rmtc_bot::parse_perception(text);
  EXPECT_FALSE(result.ok);
}

TEST(PerceptionParse, VisionRadiusOutOfRangeRejected) {
  std::string text = complete_perception();
  const std::string from = "\"vision_radius\":3";
  text.replace(text.find(from), from.size(), "\"vision_radius\":9");
  const auto result = rmtc_bot::parse_perception(text);
  EXPECT_FALSE(result.ok);
}

TEST(PerceptionParse, MissingMapRejected) {
  const auto result = rmtc_bot::parse_perception(
      R"({"tick":1,"session_id":"s","self":{"team":"A","position":[0,0],)"
      R"("hp":10,"alive":true}})");
  EXPECT_FALSE(result.ok);
}

TEST(PerceptionParse, DefaultsForOptionalFields) {
  const auto result = rmtc_bot::parse_perception(
      R"({"tick":2,"session_id":"s2","self":{"team":"A","position":[3,4],)"
      R"("hp":50,"alive":true,"defending":false,"protected":true},)"
      R"("map":{"width":8,"height":9}})");
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.perception.vision_radius, 4);
  EXPECT_TRUE(result.perception.entities.empty());
  EXPECT_TRUE(result.perception.missions.empty());
  EXPECT_EQ(result.perception.score.invalid_actions, 0.0);
  EXPECT_TRUE(result.perception.self.protected_);
  EXPECT_EQ(result.perception.self.respawn_in, 0);
}

TEST(PerceptionParse, LastActionWithoutMovedDefaultsFalse) {
  const auto result = rmtc_bot::parse_perception(
      R"({"tick":2,"session_id":"s2","self":{"team":"A","position":[3,4],)"
      R"("hp":50,"alive":true,"last_action":{"tick":1,"action":"defend"}},)"
      R"("map":{"width":8,"height":9}})");
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.perception.self.last_action.has_value());
  EXPECT_FALSE(result.perception.self.last_action->moved);
  EXPECT_EQ(result.perception.self.last_action->action, "defend");
}

TEST(PerceptionParse, InvalidLastActionRejected) {
  const auto result = rmtc_bot::parse_perception(
      R"({"tick":2,"session_id":"s2","self":{"team":"A","position":[3,4],)"
      R"("hp":50,"alive":true,"last_action":{"tick":1,"action":"move",)"
      R"("moved":"yes"}},"map":{"width":8,"height":9}})");
  EXPECT_FALSE(result.ok);
}
