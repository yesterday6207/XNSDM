#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct Area {
  int x1 = 0;
  int y1 = 0;
  int x2 = 0;
  int y2 = 0;
};

inline bool operator==(const Area& lhs, const Area& rhs) {
  return lhs.x1 == rhs.x1 && lhs.y1 == rhs.y1 && lhs.x2 == rhs.x2 && lhs.y2 == rhs.y2;
}

struct LLMEvent {
  std::string type;
  std::optional<int> active_from;
  std::optional<int> active_until;
  std::optional<Area> area;
  std::optional<Cell> point;
  std::string recommendation = "ignore";
};

struct LLMDecision {
  std::string strategy;
  std::string summary;
  std::vector<LLMEvent> events;
  int source_tick = 0;
  std::string source_session;
};

/// 校验模型输出：只接受高层判断，禁止直接动作字段。
bool validate_llm_output(const std::string& content, const std::string& session_id, int tick,
                         int map_width, int map_height, LLMDecision& out,
                         std::string& error);

}  // namespace rmtc_bot
