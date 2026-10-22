#pragma once

#include "rmtc_bot/decision/context.hpp"

namespace rmtc_bot {

/// 第一阶段：仅 MOVE/WAIT，贪心采集与未知区域探索。
Intent stage1_decide(const DecisionContext& ctx);

}  // namespace rmtc_bot
