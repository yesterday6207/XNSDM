#pragma once

#include <optional>
#include <string>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct AckReport {
  int tick = 0;
  std::string action;
  bool moved = false;
  std::optional<std::string> effective_direction;
  std::optional<std::string> error;

  std::string describe() const;
};

/// 只解释上一 Tick 回执，不用于累计定位。
std::optional<AckReport> interpret_last_action(const std::optional<LastAction>& value);

}  // namespace rmtc_bot
