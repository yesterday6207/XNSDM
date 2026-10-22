#include "rmtc_bot/action/last_action.hpp"

#include <sstream>

namespace rmtc_bot {

std::string AckReport::describe() const {
  std::ostringstream oss;
  oss << "tick=" << tick << " " << action;
  if (error.has_value()) {
    oss << " 失败 error=" << *error;
  } else if (moved) {
    oss << " 成功 direction=" << effective_direction.value_or("-");
  } else {
    oss << " 未移动";
  }
  return oss.str();
}

std::optional<AckReport> interpret_last_action(const std::optional<LastAction>& value) {
  if (!value.has_value()) {
    return std::nullopt;
  }
  AckReport report;
  report.tick = value->tick;
  report.action = value->action;
  report.moved = value->moved;
  report.effective_direction = value->effective_direction;
  report.error = value->error;
  return report;
}

}  // namespace rmtc_bot
