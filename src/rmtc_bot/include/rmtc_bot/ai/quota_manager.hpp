#pragma once

#include <mutex>
#include <optional>
#include <string>

namespace rmtc_bot {

struct QuotaSnapshot {
  std::optional<std::string> session_id;
  int calls_started = 0;
  int inflight = 0;
  bool qualified = false;
  bool exhausted = false;
  std::string last_error;
  int last_success_tick = -1;
};

/// 429/额度/超时/在途调用状态机。
class QuotaManager {
 public:
  QuotaManager(int max_calls_per_match = 40, int max_inflight = 2,
               int min_interval_ticks = 8, int periodic_interval_ticks = 80);

  void reset_session(const std::optional<std::string>& session_id);

  bool should_call(int tick, bool has_new_events);
  bool begin_call(int tick);
  void end_call(bool ok, const std::string& error_code = "", int tick = -1);
  QuotaSnapshot snapshot() const;

 private:
  int max_calls_per_match_;
  int max_inflight_;
  int min_interval_ticks_;
  int periodic_interval_ticks_;
  mutable std::mutex mutex_;

  std::optional<std::string> session_id_;
  int calls_started_ = 0;
  int inflight_ = 0;
  bool qualified_ = false;
  bool exhausted_ = false;
  int last_call_tick_ = -1;
  int next_allowed_tick_ = 0;
  std::string last_error_;
  int last_success_tick_ = -1;
};

}  // namespace rmtc_bot
