#include "rmtc_bot/decision/decision_router.hpp"

#include "rmtc_bot/decision/local_fallback.hpp"
#include "rmtc_bot/decision/stage1_collect.hpp"
#include "rmtc_bot/decision/stage2_combat.hpp"

namespace rmtc_bot {

Intent DecisionRouter::decide(const DecisionContext& ctx,
                              const std::string& decision_type) const {
  // 第一阶段只允许 MOVE/WAIT，直接强制采集策略
  if (ctx.stage == 1) {
    return stage1_decide(ctx);
  }
  if (decision_type == "stage1_collect") {
    return stage1_decide(ctx);
  }
  if (decision_type == "stage2_combat") {
    return stage2_decide(ctx);
  }
  if (decision_type == "local_fallback") {
    return local_fallback_decide(ctx, "config_local_fallback");
  }
  return local_fallback_decide(ctx, "unknown_type:" + decision_type);
}

}  // namespace rmtc_bot
