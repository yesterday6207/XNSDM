#include "rmtc_bot/action/action_sender.hpp"

namespace rmtc_bot {

ActionSender::ActionSender(PublishFn publish, Logger* logger)
    : publish_(std::move(publish)),
      logger_(logger),
      fallback_logger_("rmtc_bot.action") {
  if (logger_ == nullptr) {
    logger_ = &fallback_logger_;
  }
}

void ActionSender::reset() {
  last_sent_tick_ = -1;
  cached_.reset();
}

bool ActionSender::send(const Action& action, std::string& error) {
  std::string text;
  if (!serialize_action(action, text, error)) {
    return false;
  }
  if (action.tick <= last_sent_tick_) {
    logger_->debug("同 Tick 已发送过动作，忽略重复发送");
    error.clear();
    return false;
  }
  publish_(text);
  last_sent_tick_ = action.tick;
  cached_ = action;
  return true;
}

bool ActionSender::resend_cached(int tick, const std::string& session_id, std::string& error) {
  if (!cached_.has_value()) {
    return false;
  }
  if (cached_->tick != tick || cached_->session_id != session_id) {
    return false;
  }
  std::string text;
  if (!serialize_action(*cached_, text, error)) {
    return false;
  }
  publish_(text);
  return true;
}

std::optional<Action> ActionSender::cached() const { return cached_; }

int ActionSender::last_sent_tick() const { return last_sent_tick_; }

}  // namespace rmtc_bot
