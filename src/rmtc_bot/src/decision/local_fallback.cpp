#include "rmtc_bot/decision/local_fallback.hpp"

#include <exception>

#include "rmtc_bot/decision/stage2_combat.hpp"

namespace rmtc_bot {

Intent local_fallback_decide(const DecisionContext& ctx, const std::string& reason) {
  Intent intent;
  try {
    intent = stage2_decide(ctx);
  } catch (const std::exception&) {
    return Intent("wait", std::nullopt, "fallback_error:" + reason);
  }
  if (!reason.empty()) {
    intent.reason = intent.reason + "|fallback:" + reason;
  }
  return intent;
}

}  // namespace rmtc_bot
