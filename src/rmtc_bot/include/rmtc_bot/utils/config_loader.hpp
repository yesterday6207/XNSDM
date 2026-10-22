#pragma once

#include <string>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct AiParams {
  bool enabled = true;
  std::string base_url = "http://127.0.0.1:8000";
  std::string model;
  int max_tokens = 512;
  double temperature = 0.2;
  double request_timeout_s = 4.0;
  int max_inflight = 2;
  int max_calls_per_match = 40;
  int min_interval_ticks = 8;
  int periodic_interval_ticks = 80;
  int max_event_chars = 6000;
};

struct PlanningParams {
  Cell stage2_reach_target{12, 12};
  double low_hp_threshold = 30.0;
  double defend_hp_threshold = 45.0;
  double heal_hp_threshold = 70.0;
  int reach_priority_tick = 180;
  int hunt_after_tick = 330;
  int hunt_memory_ticks = 30;
  int patrol_interval_ticks = 50;
  int stage2_total_ticks = 500;
  int fortress_stay_ticks = 10;
  int attack_max_range = 2;
  bool exploration_enabled = true;
  int resource_memory_ticks = 120;
};

struct Params {
  int stage = 2;
  std::string team = "A";
  std::string team_name = "RMTC-Bot";
  AiParams ai;
  PlanningParams planning;
};

/// params.yaml 不存在或解析失败时返回默认参数。
Params load_params(const std::string& path);

/// decision_enum.json 解析；支持 {"decision_types": {...}}、数组或映射。
bool load_decision_enum(const std::string& path, std::vector<std::string>& out,
                        std::string& error);

/// decision_config.json 只允许 decision_type 一个字段，且必须属于枚举。
bool load_decision_config(const std::string& path, const std::string& enum_path,
                          std::string& out, std::string& error);

}  // namespace rmtc_bot
