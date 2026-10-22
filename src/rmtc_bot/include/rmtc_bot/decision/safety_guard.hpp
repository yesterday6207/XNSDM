#pragma once

#include "rmtc_bot/decision/context.hpp"

namespace rmtc_bot {

/// 动作下发前预检：阵亡、保护、阶段限制与已知不可通行格。
class SafetyGuard {
 public:
  explicit SafetyGuard(int stage);

  Intent check(const Intent& intent, const DecisionContext& ctx) const;

  int stage() const;

 private:
  int stage_;
};

}  // namespace rmtc_bot
