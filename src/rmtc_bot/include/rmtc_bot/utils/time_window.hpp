#pragma once

#include <string>

namespace rmtc_bot {

/// 异步结果必须属于当前场次且不晚于当前 Tick。
bool is_fresh(const std::string& result_session, int result_tick,
              const std::string& current_session, int current_tick,
              int max_age_ticks = 0);

bool is_current_tick(int perception_tick, int current_tick);

}  // namespace rmtc_bot
