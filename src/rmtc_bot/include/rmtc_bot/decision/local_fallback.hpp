#pragma once

#include <string>

#include "rmtc_bot/decision/context.hpp"

namespace rmtc_bot {

/// AI 超时、失败、额度耗尽时的本地降级。
Intent local_fallback_decide(const DecisionContext& ctx, const std::string& reason = "");

}  // namespace rmtc_bot
