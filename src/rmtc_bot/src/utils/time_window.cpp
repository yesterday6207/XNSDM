#include "rmtc_bot/utils/time_window.hpp"

namespace rmtc_bot {

bool is_fresh(const std::string& result_session, int result_tick,
              const std::string& current_session, int current_tick,
              int max_age_ticks) {
  if (result_session.empty() || result_session != current_session) {
    return false;
  }
  if (result_tick > current_tick) {
    return false;
  }
  if (max_age_ticks > 0 && current_tick - result_tick > max_age_ticks) {
    return false;
  }
  return true;
}

bool is_current_tick(int perception_tick, int current_tick) {
  return perception_tick >= current_tick;
}

}  // namespace rmtc_bot
