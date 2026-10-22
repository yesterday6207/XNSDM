#include "rmtc_bot/decision/safety_guard.hpp"

#include "rmtc_bot/planning/combat_rules.hpp"
#include "rmtc_bot/planning/path_planner.hpp"

namespace rmtc_bot {

SafetyGuard::SafetyGuard(int stage) : stage_(stage) {}

int SafetyGuard::stage() const { return stage_; }

Intent SafetyGuard::check(const Intent& intent, const DecisionContext& ctx) const {
  const StateStore& state = ctx.state;
  if (!state.alive) {
    return Intent("wait", std::nullopt, "safety_dead");
  }

  if (stage_ == 1 && intent.action != "move" && intent.action != "wait") {
    return Intent("wait", std::nullopt, "safety_stage1_action");
  }

  if (intent.action == "attack") {
    if (state.protected_) {
      return Intent("wait", std::nullopt, "safety_protected_attack");
    }
    if (!state.can_attack(stage_)) {
      return Intent("wait", std::nullopt, "safety_cannot_attack");
    }
    if (!intent.direction.has_value()) {
      return Intent("wait", std::nullopt, "safety_attack_no_direction");
    }
    const auto enemies = ctx.tracker.visible_enemies(ctx.perception);
    // 天下无双：5×5 攻击方向不限制目标集合，只要范围内有敌人即可
    const bool duel_ready =
        ctx.events.fortress_buff_ready(ctx.planning.fortress_stay_ticks) &&
        ctx.events.fortress_center(ctx.tick).has_value();
    if (duel_ready) {
      if (enemies_within(state.position, enemies, 2).empty()) {
        return Intent("wait", std::nullopt, "safety_attack_no_target");
      }
    } else if (!line_target(state.position, enemies, *intent.direction, 2).has_value()) {
      return Intent("wait", std::nullopt, "safety_attack_no_target");
    }
  }

  if (intent.action == "move") {
    if (!intent.direction.has_value()) {
      return Intent("wait", std::nullopt, "safety_move_no_direction");
    }
    const Cell target = move_cell(state.position, *intent.direction);
    if (!ctx.grid.in_bounds(target)) {
      return Intent("wait", std::nullopt, "safety_oob");
    }
    if (ctx.grid.is_blocked(target, ctx.tick)) {
      return Intent("wait", std::nullopt, "safety_blocked");
    }
    if (ctx.events.is_cell_blocked(target, ctx.tick)) {
      return Intent("wait", std::nullopt, "safety_event_blocked");
    }
    const auto enemy_cells = ctx.tracker.enemy_cells(ctx.perception);
    if (enemy_cells.count(target) > 0) {
      return Intent("wait", std::nullopt, "safety_occupied");
    }
  }

  if (intent.action == "defend" && !state.can_defend(stage_)) {
    return Intent("wait", std::nullopt, "safety_cannot_defend");
  }

  return intent;
}

}  // namespace rmtc_bot
