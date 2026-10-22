#include "rmtc_bot/ai/output_validator.hpp"

#include <json/json.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#include <sstream>

namespace rmtc_bot {

namespace {

const std::set<std::string> kEventTypes{"explosion", "illusion", "angel", "duel",
                                        "world_split", "pepper", "clean", "panpan", "other"};
const std::set<std::string> kStrategies{"collect", "reach",   "fight", "defend",
                                        "explore", "heal",    "fortress", "avoid"};
const std::set<std::string> kRecommendations{"avoid", "enter", "defend", "attack",
                                             "ignore", "fortress", "heal"};
const std::set<std::string> kForbiddenKeys{"action", "actions", "direction", "moves",
                                           "commands", "tick_action"};

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string to_lower(const std::string& text) {
  std::string result = text;
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return result;
}

std::string strip_code_fence(const std::string& text) {
  std::string cleaned = trim(text);
  if (cleaned.rfind("```", 0) != 0) {
    return cleaned;
  }
  std::vector<std::string> lines;
  std::istringstream stream(cleaned);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  if (!lines.empty()) {
    lines.erase(lines.begin());
  }
  if (!lines.empty() && trim(lines.back()).rfind("```", 0) == 0) {
    lines.pop_back();
  }
  std::ostringstream oss;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      oss << "\n";
    }
    oss << lines[i];
  }
  return trim(oss.str());
}

bool check_forbidden(const Json::Value& mapping, const std::string& path,
                     std::string& error) {
  if (!mapping.isObject()) {
    return true;
  }
  for (const auto& key : mapping.getMemberNames()) {
    if (kForbiddenKeys.count(to_lower(key)) > 0) {
      error = path + " 含禁止的直接动作字段: " + key;
      return false;
    }
  }
  return true;
}

bool parse_time(const Json::Value& value, const std::string& name, std::optional<int>& out,
                std::string& error) {
  if (value.isNull()) {
    out = std::nullopt;
    return true;
  }
  if (!value.isInt() && !value.isUInt()) {
    error = name + " 必须是非负整数或 null";
    return false;
  }
  const int parsed = value.asInt();
  if (parsed < 0) {
    error = name + " 必须是非负整数或 null";
    return false;
  }
  out = parsed;
  return true;
}

bool is_int_value(const Json::Value& value) { return value.isInt() || value.isUInt(); }

bool parse_area(const Json::Value& value, int width, int height, std::optional<Area>& out,
                std::string& error) {
  if (value.isNull()) {
    out = std::nullopt;
    return true;
  }
  if (!value.isArray() || value.size() != 4) {
    error = "area 必须是 [x1,y1,x2,y2] 或 null";
    return false;
  }
  for (const auto& item : value) {
    if (!is_int_value(item)) {
      error = "area 坐标必须是整数";
      return false;
    }
  }
  int x1 = value[0].asInt();
  int y1 = value[1].asInt();
  int x2 = value[2].asInt();
  int y2 = value[3].asInt();
  if (x1 > x2) {
    std::swap(x1, x2);
  }
  if (y1 > y2) {
    std::swap(y1, y2);
  }
  x1 = std::max(0, std::min(width - 1, x1));
  x2 = std::max(0, std::min(width - 1, x2));
  y1 = std::max(0, std::min(height - 1, y1));
  y2 = std::max(0, std::min(height - 1, y2));
  out = Area{x1, y1, x2, y2};
  return true;
}

bool parse_point(const Json::Value& value, int width, int height,
                 std::optional<Cell>& out, std::string& error) {
  if (value.isNull()) {
    out = std::nullopt;
    return true;
  }
  if (!value.isArray() || value.size() != 2) {
    error = "point 必须是 [x,y] 或 null";
    return false;
  }
  if (!is_int_value(value[0]) || !is_int_value(value[1])) {
    error = "point 坐标必须是整数";
    return false;
  }
  const int x = value[0].asInt();
  const int y = value[1].asInt();
  if (x < 0 || x >= width || y < 0 || y >= height) {
    out = std::nullopt;
    return true;
  }
  out = Cell{x, y};
  return true;
}

}  // namespace

bool validate_llm_output(const std::string& content, const std::string& session_id, int tick,
                         int map_width, int map_height, LLMDecision& out,
                         std::string& error) {
  error.clear();
  if (trim(content).empty()) {
    error = "模型输出为空";
    return false;
  }
  const std::string text = strip_code_fence(content);

  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value data;
  std::string errors;
  if (!reader->parse(text.data(), text.data() + text.size(), &data, &errors)) {
    error = "模型输出不是合法 JSON: " + errors;
    return false;
  }
  if (!data.isObject()) {
    error = "模型输出顶层必须是 JSON 对象";
    return false;
  }
  if (!check_forbidden(data, "顶层", error)) {
    return false;
  }

  if (!data["strategy"].isString() ||
      kStrategies.count(data["strategy"].asString()) == 0) {
    error = "strategy 必须是允许的枚举之一";
    return false;
  }
  const std::string strategy = data["strategy"].asString();

  std::string summary;
  if (!data["summary"].isNull()) {
    if (!data["summary"].isString()) {
      error = "summary 必须是字符串";
      return false;
    }
    summary = data["summary"].asString();
    if (summary.size() > 500) {
      summary.resize(500);
    }
  }

  std::vector<LLMEvent> events;
  if (!data["events"].isNull()) {
    if (!data["events"].isArray()) {
      error = "events 必须是数组";
      return false;
    }
    if (data["events"].size() > 16) {
      error = "events 数量超过上限";
      return false;
    }
    int index = 0;
    for (const auto& raw : data["events"]) {
      const std::string path = "events[" + std::to_string(index) + "]";
      ++index;
      if (!raw.isObject()) {
        error = path + " 必须是对象";
        return false;
      }
      if (!check_forbidden(raw, path, error)) {
        return false;
      }
      if (!raw["type"].isString() || kEventTypes.count(raw["type"].asString()) == 0) {
        error = path + ".type 非法";
        return false;
      }
      LLMEvent event;
      event.type = raw["type"].asString();
      if (!parse_time(raw["active_from"], path + ".active_from", event.active_from, error)) {
        return false;
      }
      if (!parse_time(raw["active_until"], path + ".active_until", event.active_until,
                      error)) {
        return false;
      }
      if (event.active_from.has_value() && event.active_until.has_value() &&
          *event.active_from > *event.active_until) {
        error = path + " 生效区间倒置";
        return false;
      }
      if (event.active_until.has_value() && *event.active_until <= tick) {
        continue;
      }
      if (!parse_area(raw["area"], map_width, map_height, event.area, error)) {
        return false;
      }
      if (!parse_point(raw["point"], map_width, map_height, event.point, error)) {
        return false;
      }
      if (!raw["recommendation"].isNull()) {
        if (!raw["recommendation"].isString() ||
            kRecommendations.count(raw["recommendation"].asString()) == 0) {
          error = path + ".recommendation 非法";
          return false;
        }
        event.recommendation = raw["recommendation"].asString();
      }
      events.push_back(event);
    }
  }

  out.strategy = strategy;
  out.summary = summary;
  out.events = std::move(events);
  out.source_tick = tick;
  out.source_session = session_id;
  return true;
}

}  // namespace rmtc_bot
