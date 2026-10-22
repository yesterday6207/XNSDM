#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct ParseResult {
  bool ok = false;
  Perception perception;
  std::string error;
  std::optional<int> tick;
  std::optional<std::string> session_id;
};

/// 解析感知 JSON。失败不抛异常，返回 ok=false 与原因。
ParseResult parse_perception(const std::string& data);

}  // namespace rmtc_bot
