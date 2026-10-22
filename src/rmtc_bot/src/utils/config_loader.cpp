#include "rmtc_bot/utils/config_loader.hpp"

#include <json/json.h>
#include <yaml-cpp/yaml.h>

#include <fstream>
#include <sstream>

namespace rmtc_bot {

namespace {

template <typename T>
bool read_value(const YAML::Node& node, const char* key, T& out) {
  if (!node || !node[key]) {
    return false;
  }
  try {
    out = node[key].as<T>();
    return true;
  } catch (const YAML::Exception&) {
    return false;
  }
}

void read_cell(const YAML::Node& node, const char* key, Cell& out) {
  if (!node || !node[key] || !node[key].IsSequence() || node[key].size() != 2) {
    return;
  }
  try {
    out = Cell{node[key][0].as<int>(), node[key][1].as<int>()};
  } catch (const YAML::Exception&) {
  }
}

void apply_params(const YAML::Node& root, Params& params) {
  read_value(root, "stage", params.stage);
  read_value(root, "team", params.team);
  read_value(root, "team_name", params.team_name);

  if (root["ai"] && root["ai"].IsMap()) {
    const YAML::Node ai = root["ai"];
    read_value(ai, "enabled", params.ai.enabled);
    read_value(ai, "base_url", params.ai.base_url);
    read_value(ai, "model", params.ai.model);
    read_value(ai, "max_tokens", params.ai.max_tokens);
    read_value(ai, "temperature", params.ai.temperature);
    read_value(ai, "request_timeout_s", params.ai.request_timeout_s);
    read_value(ai, "max_inflight", params.ai.max_inflight);
    read_value(ai, "max_calls_per_match", params.ai.max_calls_per_match);
    read_value(ai, "min_interval_ticks", params.ai.min_interval_ticks);
    read_value(ai, "periodic_interval_ticks", params.ai.periodic_interval_ticks);
    read_value(ai, "max_event_chars", params.ai.max_event_chars);
  }

  if (root["planning"] && root["planning"].IsMap()) {
    const YAML::Node planning = root["planning"];
    read_cell(planning, "stage2_reach_target", params.planning.stage2_reach_target);
    read_value(planning, "low_hp_threshold", params.planning.low_hp_threshold);
    read_value(planning, "defend_hp_threshold", params.planning.defend_hp_threshold);
    read_value(planning, "heal_hp_threshold", params.planning.heal_hp_threshold);
    read_value(planning, "reach_priority_tick", params.planning.reach_priority_tick);
    read_value(planning, "hunt_after_tick", params.planning.hunt_after_tick);
    read_value(planning, "hunt_memory_ticks", params.planning.hunt_memory_ticks);
    read_value(planning, "patrol_interval_ticks", params.planning.patrol_interval_ticks);
    read_value(planning, "stage2_total_ticks", params.planning.stage2_total_ticks);
    read_value(planning, "fortress_stay_ticks", params.planning.fortress_stay_ticks);
    read_value(planning, "attack_max_range", params.planning.attack_max_range);
    read_value(planning, "exploration_enabled", params.planning.exploration_enabled);
    read_value(planning, "resource_memory_ticks", params.planning.resource_memory_ticks);
  }
}

bool parse_json_file(const std::string& path, Json::Value& root, std::string& error) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    error = "文件不存在或无法打开: " + path;
    return false;
  }
  Json::CharReaderBuilder builder;
  std::string errors;
  if (!Json::parseFromStream(builder, stream, &root, &errors)) {
    error = "不是合法 JSON: " + errors;
    return false;
  }
  return true;
}

}  // namespace

Params load_params(const std::string& path) {
  Params params;
  std::ifstream probe(path);
  if (!probe.is_open()) {
    return params;
  }
  probe.close();
  try {
    const YAML::Node root = YAML::LoadFile(path);
    if (!root.IsMap()) {
      return params;
    }
    apply_params(root, params);
  } catch (const YAML::Exception&) {
    return params;
  }
  return params;
}

bool load_decision_enum(const std::string& path, std::vector<std::string>& out,
                        std::string& error) {
  out.clear();
  Json::Value root;
  if (!parse_json_file(path, root, error)) {
    error = "决策枚举" + error;
    return false;
  }

  Json::Value values = root;
  if (root.isObject() && root.isMember("decision_types")) {
    values = root["decision_types"];
  }

  if (values.isObject()) {
    for (const auto& key : values.getMemberNames()) {
      out.push_back(key);
    }
  } else if (values.isArray()) {
    for (const auto& item : values) {
      if (item.isString()) {
        if (item.asString().empty()) {
          error = "decision_enum.json 中存在非法类型标识";
          out.clear();
          return false;
        }
        out.push_back(item.asString());
      } else if (item.isObject() && item.isMember("id") && item["id"].isString()) {
        if (item["id"].asString().empty()) {
          error = "decision_enum.json 中存在非法类型标识";
          out.clear();
          return false;
        }
        out.push_back(item["id"].asString());
      } else {
        error = "decision_enum.json 中存在非法类型标识";
        out.clear();
        return false;
      }
    }
  } else {
    error = "decision_enum.json 格式不合法";
    out.clear();
    return false;
  }

  if (out.empty()) {
    error = "decision_enum.json 中没有登记任何决策类型";
    return false;
  }
  return true;
}

bool load_decision_config(const std::string& path, const std::string& enum_path,
                          std::string& out, std::string& error) {
  out.clear();
  Json::Value root;
  if (!parse_json_file(path, root, error)) {
    error = "decision_config.json " + error;
    return false;
  }
  if (!root.isObject()) {
    error = "decision_config.json 顶层必须是对象";
    return false;
  }
  for (const auto& key : root.getMemberNames()) {
    if (key != "decision_type") {
      error = "decision_config.json 只允许 decision_type 一个字段，发现: " + key;
      return false;
    }
  }
  if (!root.isMember("decision_type") || !root["decision_type"].isString() ||
      root["decision_type"].asString().empty()) {
    error = "decision_type 必须是非空字符串";
    return false;
  }
  const std::string decision_type = root["decision_type"].asString();

  std::vector<std::string> enum_values;
  std::string enum_error;
  if (!load_decision_enum(enum_path, enum_values, enum_error)) {
    error = enum_error;
    return false;
  }
  for (const auto& item : enum_values) {
    if (item == decision_type) {
      out = decision_type;
      return true;
    }
  }
  std::ostringstream oss;
  oss << "decision_type=" << decision_type << " 未在赛前枚举中登记，可用: ";
  for (std::size_t i = 0; i < enum_values.size(); ++i) {
    if (i > 0) {
      oss << ", ";
    }
    oss << enum_values[i];
  }
  error = oss.str();
  return false;
}

}  // namespace rmtc_bot
