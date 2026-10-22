#pragma once

#include <string>

#include "rmtc_bot/decision/context.hpp"

namespace rmtc_bot {

/// 按 decision_type 路由到已封存策略。
class DecisionRouter {
 public:
  Intent decide(const DecisionContext& ctx, const std::string& decision_type) const;
};

}  // namespace rmtc_bot
