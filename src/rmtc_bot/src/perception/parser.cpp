#include "rmtc_bot/perception/parser.hpp"

#include <json/json.h>

#include <memory>
#include <set>
#include <sstream>

#include "rmtc_bot/perception/relative_map.hpp"

namespace rmtc_bot {

namespace {

const std::set<std::string> kKnownEntityTypes{"robot", "resource_normal", "resource_high",
                                              "obstacle"};

bool is_int_value(const Json::Value& value) { return value.isInt() || value.isUInt(); }

bool parse_json(const std::string& data, Json::Value& root, std::string& error) {
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  std::string errors;
  if (!reader->parse(data.data(), data.data() + data.size(), &root, &errors)) {
    error = "感知不是合法 JSON: " + errors;
    return false;
  }
  return true;
}

bool read_position(const Json::Value& value, const std::string& name, Cell& out,
                   std::string& error) {
  if (!value.isArray() || value.size() != 2) {
    error = name + " 必须是长度 2 的坐标数组";
    return false;
  }
  if (!is_int_value(value[0]) || !is_int_value(value[1])) {
    error = name + " 坐标必须是整数";
    return false;
  }
  out = Cell{value[0].asInt(), value[1].asInt()};
  return true;
}

bool read_int(const Json::Value& value, const std::string& name, int& out, std::string& error,
              std::optional<int> minimum = std::nullopt) {
  if (!is_int_value(value)) {
    error = name + " 必须是整数";
    return false;
  }
  const int candidate = value.asInt();
  if (minimum.has_value() && candidate < *minimum) {
    error = name + " 不能小于 " + std::to_string(*minimum);
    return false;
  }
  out = candidate;
  return true;
}

bool read_bool(const Json::Value& value, const std::string& name, bool& out, std::string& error,
               bool default_value = false) {
  if (value.isNull()) {
    out = default_value;
    return true;
  }
  if (!value.isBool()) {
    error = name + " 必须是布尔值";
    return false;
  }
  out = value.asBool();
  return true;
}

bool parse_last_action(const Json::Value& value, std::optional<LastAction>& out,
                       std::string& error) {
  if (value.isNull()) {
    out = std::nullopt;
    return true;
  }
  if (!value.isObject()) {
    error = "self.last_action 必须是对象或 null";
    return false;
  }
  LastAction last;
  if (!read_int(value["tick"], "self.last_action.tick", last.tick, error)) {
    return false;
  }
  if (!value["action"].isString()) {
    error = "self.last_action.action 必须是字符串";
    return false;
  }
  last.action = value["action"].asString();
  // moved 属于回执细节，缺失时按未移动处理，避免因可选字段缺失整帧被拒
  if (!value["moved"].isNull()) {
    if (!value["moved"].isBool()) {
      error = "self.last_action.moved 必须是布尔值";
      return false;
    }
    last.moved = value["moved"].asBool();
  }
  if (!value["effective_direction"].isNull()) {
    if (!value["effective_direction"].isString()) {
      error = "self.last_action.effective_direction 必须是字符串";
      return false;
    }
    last.effective_direction = value["effective_direction"].asString();
  }
  if (!value["error"].isNull()) {
    if (!value["error"].isString()) {
      error = "self.last_action.error 必须是字符串";
      return false;
    }
    last.error = value["error"].asString();
  }
  out = last;
  return true;
}

bool parse_self(const Json::Value& value, SelfState& out, std::string& error) {
  if (!value.isObject()) {
    error = "self 必须是对象";
    return false;
  }
  if (!value["team"].isString() || value["team"].asString().empty()) {
    error = "self.team 必须是非空字符串";
    return false;
  }
  out.team = value["team"].asString();

  if (value.isMember("robot_id")) {
    if (!value["robot_id"].isString() || value["robot_id"].asString().empty()) {
      error = "self.robot_id 必须是非空字符串";
      return false;
    }
    out.robot_id = value["robot_id"].asString();
  } else {
    out.robot_id = out.team;
  }

  if (!read_position(value["position"], "self.position", out.position, error)) {
    return false;
  }
  if (!value["hp"].isNumeric()) {
    error = "self.hp 必须是数值";
    return false;
  }
  out.hp = value["hp"].asDouble();

  if (!value["alive"].isBool()) {
    error = "self.alive 必须是布尔值";
    return false;
  }
  out.alive = value["alive"].asBool();

  if (!read_bool(value["defending"], "self.defending", out.defending, error)) {
    return false;
  }
  if (!read_bool(value["protected"], "self.protected", out.protected_, error)) {
    return false;
  }

  if (!value["respawn_in"].isNull() &&
      !read_int(value["respawn_in"], "self.respawn_in", out.respawn_in, error, 0)) {
    return false;
  }
  if (!value["position_reason"].isNull()) {
    if (!value["position_reason"].isString()) {
      error = "self.position_reason 必须是字符串";
      return false;
    }
    out.position_reason = value["position_reason"].asString();
  }

  std::optional<LastAction> last_action;
  if (!parse_last_action(value["last_action"], last_action, error)) {
    return false;
  }
  out.last_action = last_action;
  return true;
}

std::optional<VisibleEntity> parse_entity(const Json::Value& raw, const Cell& self_position) {
  if (!raw.isObject()) {
    return std::nullopt;
  }
  if (!raw["type"].isString() || kKnownEntityTypes.count(raw["type"].asString()) == 0) {
    return std::nullopt;
  }
  if (!raw["position"].isString()) {
    return std::nullopt;
  }
  std::string position_error;
  const auto position = to_absolute(self_position, raw["position"].asString(), position_error);
  if (!position.has_value()) {
    return std::nullopt;
  }

  const std::string entity_type = raw["type"].asString();
  VisibleEntity entity;
  entity.type = entity_type;
  entity.position = *position;

  if (entity_type == "obstacle") {
    std::ostringstream oss;
    oss << "obstacle:" << position->first << ":" << position->second;
    entity.id = oss.str();
    return entity;
  }

  if (entity_type == "robot") {
    if (!raw["robot_id"].isString() || raw["robot_id"].asString().empty()) {
      return std::nullopt;
    }
    entity.robot_id = raw["robot_id"].asString();
    entity.id = raw.isMember("id") && raw["id"].isString() ? raw["id"].asString()
                                                           : *entity.robot_id;
    if (raw["hp"].isNumeric()) {
      entity.hp = raw["hp"].asDouble();
    }
    if (raw["team"].isString()) {
      entity.team = raw["team"].asString();
    }
    std::string ignored;
    if (!read_bool(raw["defending"], "entity.defending", entity.defending, ignored)) {
      return std::nullopt;
    }
    if (!read_bool(raw["protected"], "entity.protected", entity.protected_, ignored)) {
      return std::nullopt;
    }
    return entity;
  }

  // resource_normal / resource_high
  if (!raw["id"].isString() || raw["id"].asString().empty()) {
    return std::nullopt;
  }
  entity.id = raw["id"].asString();
  if (raw["value"].isNumeric()) {
    entity.value = static_cast<int>(raw["value"].asDouble());
  }
  return entity;
}

std::vector<Mission> parse_missions(const Json::Value& value) {
  std::vector<Mission> result;
  if (!value.isArray()) {
    return result;
  }
  for (const auto& raw : value) {
    if (!raw.isObject()) {
      continue;
    }
    if (!raw["id"].isString() || !raw["type"].isString()) {
      continue;
    }
    Mission mission;
    mission.id = raw["id"].asString();
    mission.type = raw["type"].asString();
    if (raw["target"].isNumeric()) {
      mission.target = raw["target"].asDouble();
    }
    if (!raw["position"].isNull()) {
      Cell position;
      std::string ignored;
      if (read_position(raw["position"], "mission.position", position, ignored)) {
        mission.position = position;
      }
    }
    result.push_back(mission);
  }
  return result;
}

Score parse_score(const Json::Value& value) {
  Score score;
  if (!value.isObject()) {
    return score;
  }
  if (value["resource_value"].isNumeric()) {
    score.resource_value = value["resource_value"].asDouble();
  }
  if (value["effective_damage"].isNumeric()) {
    score.effective_damage = value["effective_damage"].asDouble();
  }
  if (value["mission_completed"].isNumeric()) {
    score.mission_completed = value["mission_completed"].asDouble();
  }
  if (value["invalid_actions"].isNumeric()) {
    score.invalid_actions = value["invalid_actions"].asDouble();
  }
  if (value["mission_completed_ids"].isArray()) {
    for (const auto& item : value["mission_completed_ids"]) {
      if (item.isString()) {
        score.mission_completed_ids.push_back(item.asString());
      }
    }
  }
  return score;
}

ParseResult fail(ParseResult result, const std::string& message) {
  result.ok = false;
  result.error = message;
  return result;
}

}  // namespace

ParseResult parse_perception(const std::string& data) {
  ParseResult result;
  Json::Value root;
  std::string json_error;
  if (!parse_json(data, root, json_error)) {
    result.error = json_error;
    return result;
  }
  if (!root.isObject()) {
    result.error = "感知 JSON 顶层必须是对象";
    return result;
  }

  const Json::Value& tick_raw = root["tick"];
  const Json::Value& session_raw = root["session_id"];
  if (is_int_value(tick_raw)) {
    result.tick = tick_raw.asInt();
  }
  if (session_raw.isString()) {
    result.session_id = session_raw.asString();
  }

  Perception perception;
  std::string error;

  if (!read_int(tick_raw, "tick", perception.tick, error, 0)) {
    return fail(result, error);
  }
  if (!session_raw.isString() || session_raw.asString().empty()) {
    return fail(result, "session_id 必须是非空字符串");
  }
  perception.session_id = session_raw.asString();

  if (!parse_self(root["self"], perception.self, error)) {
    return fail(result, error);
  }

  if (!root["vision_radius"].isNull()) {
    if (!read_int(root["vision_radius"], "vision_radius", perception.vision_radius, error)) {
      return fail(result, error);
    }
  }
  if (perception.vision_radius < 0 || perception.vision_radius > 4) {
    return fail(result, "vision_radius 必须在 0..4 之间");
  }

  if (!root["visible_entities"].isNull()) {
    const Json::Value& entities_raw = root["visible_entities"];
    if (!entities_raw.isArray()) {
      return fail(result, "visible_entities 必须是数组");
    }
    for (const auto& item : entities_raw) {
      const auto entity = parse_entity(item, perception.self.position);
      if (entity.has_value()) {
        perception.entities.push_back(*entity);
      }
    }
  }

  if (!root["map"].isObject()) {
    return fail(result, "map 必须是对象");
  }
  if (!read_int(root["map"]["width"], "map.width", perception.map_width, error, 1)) {
    return fail(result, error);
  }
  if (!read_int(root["map"]["height"], "map.height", perception.map_height, error, 1)) {
    return fail(result, error);
  }

  if (!root["mission_text"].isNull()) {
    if (!root["mission_text"].isString()) {
      return fail(result, "mission_text 必须是字符串");
    }
    perception.mission_text = root["mission_text"].asString();
  }

  if (!root["event_text"].isNull()) {
    if (!root["event_text"].isString()) {
      return fail(result, "event_text 必须是字符串");
    }
    perception.event_text = root["event_text"].asString();
  }

  if (!root["missions"].isNull() && !root["missions"].isArray()) {
    return fail(result, "missions 必须是数组");
  }
  perception.missions = parse_missions(root["missions"]);
  perception.score = parse_score(root["score"]);

  result.ok = true;
  result.error.clear();
  result.perception = std::move(perception);
  return result;
}

}  // namespace rmtc_bot
