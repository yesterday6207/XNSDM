#include "rmtc_bot/action/action_schema.hpp"

#include <json/json.h>

#include <memory>
#include <set>
#include <sstream>

namespace rmtc_bot {

namespace {

const std::set<std::string> kValidActions{"move", "attack", "defend", "wait"};
const std::set<std::string> kValidDirections{"up", "down", "left", "right"};
constexpr std::size_t kMaxActionBytes = 4096;

bool is_lowercase(const std::string& value) {
  for (const char ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool validate_action(const Action& action, std::string& error) {
  error.clear();
  if (action.tick < 0) {
    error = "tick 不能为负数";
    return false;
  }
  if (action.session_id.empty()) {
    error = "session_id 必须是非空字符串";
    return false;
  }
  if (kValidActions.count(action.action) == 0) {
    error = "action 必须是 move/attack/defend/wait 之一";
    return false;
  }
  if (!is_lowercase(action.action)) {
    error = "action 必须小写";
    return false;
  }
  if (action.action == "move" || action.action == "attack") {
    if (!action.direction.has_value() || kValidDirections.count(*action.direction) == 0) {
      error = action.action + " 必须带合法 direction";
      return false;
    }
    if (!is_lowercase(*action.direction)) {
      error = "direction 必须小写";
      return false;
    }
  } else if (action.direction.has_value()) {
    error = action.action + " 不应携带 direction";
    return false;
  }
  return true;
}

bool build_action(int tick, const std::string& session_id, const std::string& action,
                  const std::optional<std::string>& direction, Action& out,
                  std::string& error) {
  Action candidate;
  candidate.tick = tick;
  candidate.session_id = session_id;
  candidate.action = action;
  candidate.direction = direction;
  if (!validate_action(candidate, error)) {
    return false;
  }
  out = candidate;
  return true;
}

bool serialize_action(const Action& action, std::string& out, std::string& error) {
  if (!validate_action(action, error)) {
    return false;
  }
  Json::Value root(Json::objectValue);
  root["tick"] = action.tick;
  root["session_id"] = action.session_id;
  root["action"] = action.action;
  if (action.direction.has_value()) {
    root["direction"] = *action.direction;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["emitUTF8"] = true;
  const std::string text = Json::writeString(builder, root);
  if (text.size() > kMaxActionBytes) {
    error = "动作消息超过 4096 字节";
    return false;
  }
  out = text;
  return true;
}

bool parse_action_json(const std::string& text, Action& out, std::string& error) {
  error.clear();
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value root;
  std::string errors;
  if (!reader->parse(text.data(), text.data() + text.size(), &root, &errors)) {
    error = "动作不是合法 JSON: " + errors;
    return false;
  }
  if (!root.isObject()) {
    error = "动作必须是 JSON 对象";
    return false;
  }
  for (const auto& key : root.getMemberNames()) {
    if (key != "tick" && key != "session_id" && key != "action" && key != "direction") {
      error = "动作包含未知字段: " + key;
      return false;
    }
  }
  if (!root.isMember("tick") || !root.isMember("session_id") || !root.isMember("action")) {
    error = "动作缺少必要字段";
    return false;
  }
  if (!root["tick"].isInt() && !root["tick"].isUInt()) {
    error = "tick 必须是整数";
    return false;
  }
  if (!root["session_id"].isString()) {
    error = "session_id 必须是非空字符串";
    return false;
  }
  if (!root["action"].isString()) {
    error = "action 必须是字符串";
    return false;
  }
  Action candidate;
  candidate.tick = root["tick"].asInt();
  candidate.session_id = root["session_id"].asString();
  candidate.action = root["action"].asString();
  if (root.isMember("direction")) {
    if (!root["direction"].isString()) {
      error = "direction 必须是字符串";
      return false;
    }
    candidate.direction = root["direction"].asString();
  }
  if (!validate_action(candidate, error)) {
    return false;
  }
  out = candidate;
  return true;
}

}  // namespace rmtc_bot
