#pragma once

#include "rmtc_bot/decision/context.hpp"

namespace rmtc_bot {

/// 第二阶段：资源、任务、对抗、防御与事件规避综合策略。
Intent stage2_decide(const DecisionContext& ctx);

}  // namespace rmtc_bot
