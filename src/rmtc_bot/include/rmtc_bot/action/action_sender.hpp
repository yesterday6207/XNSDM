#pragma once

#include <functional>
#include <optional>
#include <string>

#include "rmtc_bot/action/action_schema.hpp"
#include "rmtc_bot/utils/logger.hpp"

namespace rmtc_bot {

/// 动作发布与同 Tick 单次发送控制。
class ActionSender {
 public:
  using PublishFn = std::function<void(const std::string&)>;

  explicit ActionSender(PublishFn publish, Logger* logger = nullptr);

  void reset();

  /// 同一 Tick 只发送一次；返回是否真正发布。
  bool send(const Action& action, std::string& error);

  /// 同 Tick 感知重发时重发缓存动作，不推进任何规划状态。
  bool resend_cached(int tick, const std::string& session_id, std::string& error);

  std::optional<Action> cached() const;
  int last_sent_tick() const;

 private:
  PublishFn publish_;
  Logger* logger_;
  int last_sent_tick_ = -1;
  std::optional<Action> cached_;
  Logger fallback_logger_;
};

}  // namespace rmtc_bot
