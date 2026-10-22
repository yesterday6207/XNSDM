#include "rmtc_bot/ai/quota_manager.hpp"

#include <algorithm>

namespace rmtc_bot {

QuotaManager::QuotaManager(int max_calls_per_match, int max_inflight,
                           int min_interval_ticks, int periodic_interval_ticks)
    : max_calls_per_match_(std::max(1, max_calls_per_match)),
      max_inflight_(std::max(1, max_inflight)),
      min_interval_ticks_(std::max(0, min_interval_ticks)),
      periodic_interval_ticks_(std::max(1, periodic_interval_ticks)) {
  reset_session(std::nullopt);
}

void QuotaManager::reset_session(const std::optional<std::string>& session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  session_id_ = session_id;
  calls_started_ = 0;
  inflight_ = 0;
  qualified_ = false;
  exhausted_ = false;
  last_call_tick_ = -1;
  next_allowed_tick_ = 0;
  last_error_.clear();
  last_success_tick_ = -1;
}

bool QuotaManager::should_call(int tick, bool has_new_events) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (exhausted_) {
    return false;
  }
  if (calls_started_ >= max_calls_per_match_) {
    return false;
  }
  if (inflight_ >= max_inflight_) {
    return false;
  }
  if (tick < next_allowed_tick_) {
    return false;
  }
  if (has_new_events) {
    return true;
  }
  if (last_call_tick_ < 0) {
    return true;
  }
  if (!qualified_ && tick <= 5) {
    return true;
  }
  return tick - last_call_tick_ >= periodic_interval_ticks_;
}

bool QuotaManager::begin_call(int tick) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (exhausted_ || calls_started_ >= max_calls_per_match_) {
    return false;
  }
  if (inflight_ >= max_inflight_) {
    return false;
  }
  ++calls_started_;
  ++inflight_;
  last_call_tick_ = tick;
  next_allowed_tick_ = tick + min_interval_ticks_;
  return true;
}

void QuotaManager::end_call(bool ok, const std::string& error_code, int tick) {
  std::lock_guard<std::mutex> lock(mutex_);
  inflight_ = std::max(0, inflight_ - 1);
  if (ok) {
    qualified_ = true;
    last_success_tick_ = tick;
    last_error_.clear();
    return;
  }
  last_error_ = error_code.empty() ? "unknown" : error_code;
  if (error_code == "quota_exceeded") {
    exhausted_ = true;
  } else if (error_code == "rate_limit" || error_code == "concurrency_limit") {
    next_allowed_tick_ = std::max(next_allowed_tick_, tick + 4);
  }
}

QuotaSnapshot QuotaManager::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  QuotaSnapshot result;
  result.session_id = session_id_;
  result.calls_started = calls_started_;
  result.inflight = inflight_;
  result.qualified = qualified_;
  result.exhausted = exhausted_;
  result.last_error = last_error_;
  result.last_success_tick = last_success_tick_;
  return result;
}

}  // namespace rmtc_bot
