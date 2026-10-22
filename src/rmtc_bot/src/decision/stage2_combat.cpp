#include "rmtc_bot/decision/stage2_combat.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "rmtc_bot/planning/combat_rules.hpp"
#include "rmtc_bot/planning/path_planner.hpp"
#include "rmtc_bot/planning/target_selector.hpp"

namespace rmtc_bot {

namespace {

/// 资源超过该 Tick 数仍未再被确认，视为可能已被对手收集。
constexpr int kResourceStaleTicks = 60;
/// 资源未被当前视野确认时，若最近该 Tick 内看到敌人在其旁，视为已被抢。
constexpr int kContestedEnemyTicks = 8;
/// 后期只顺路收集该距离内的资源，其余 Tick 用于输出。
constexpr int kLatePickupDistance = 3;
/// 前期进入任务点冲刺后，只在新资源不超过该距离时顺路拾取。
constexpr int kCheapResourceDistance = 4;
/// LLM 策略偏置：fight 提前进入输出期，collect 推后并扩大顺路拾取范围。
constexpr int kLlmEarlyHunt = 60;
constexpr int kLlmLateHunt = 40;
constexpr int kLlmLatePickupDistance = 6;
constexpr double kLlmDefendBonus = 10.0;
constexpr double kLlmHealBonus = 15.0;
/// 前期换血门槛：资源价值高于一次普通攻击，血量不健康时不接战。
constexpr double kEarlyTradeHp = 70.0;
/// 撤离时把最近见过的敌人也当作威胁，避免离开视野后立刻折返。
constexpr int kThreatMemoryTicks = 6;

std::optional<Cell> reach_target(const DecisionContext& ctx) {
  return ctx.missions.reach_target(ctx.planning.stage2_reach_target);
}

int manhattan(const Cell& lhs, const Cell& rhs) {
  return std::abs(lhs.first - rhs.first) + std::abs(lhs.second - rhs.second);
}

int chebyshev(const Cell& lhs, const Cell& rhs) {
  return std::max(std::abs(lhs.first - rhs.first), std::abs(lhs.second - rhs.second));
}

std::string direction_toward(const Cell& from, const Cell& to) {
  const int dx = to.first - from.first;
  const int dy = to.second - from.second;
  if (std::abs(dx) >= std::abs(dy)) {
    if (dx > 0) {
      return "right";
    }
    if (dx < 0) {
      return "left";
    }
  }
  if (dy > 0) {
    return "down";
  }
  if (dy < 0) {
    return "up";
  }
  return "up";
}

/// 该格是否可安全落脚：在界内、无障碍/事件墙/风险、且不被敌人占位。
bool legal_cell(const DecisionContext& ctx, const Cell& cell, const BlockedFn& blocked,
                const CellSet& extra) {
  if (!ctx.grid.in_bounds(cell)) {
    return false;
  }
  if (blocked(cell)) {
    return false;
  }
  return extra.count(cell) == 0;
}

/// 目标格被敌人占住时，改用它旁边离自己最近的可通行格。
std::optional<Cell> approach_cell(const DecisionContext& ctx, const Cell& goal,
                                  const BlockedFn& blocked, const CellSet& extra) {
  if (extra.count(goal) == 0) {
    return goal;
  }
  std::optional<Cell> best;
  int best_distance = std::numeric_limits<int>::max();
  for (const auto& direction : kDirections) {
    const Cell cell = move_cell(goal, direction.name);
    if (!legal_cell(ctx, cell, blocked, extra)) {
      continue;
    }
    const int distance = manhattan(ctx.state.position, cell);
    if (distance < best_distance || (distance == best_distance && (!best.has_value() ||
                                                                  cell < *best))) {
      best = cell;
      best_distance = distance;
    }
  }
  return best;
}

/// 前往目标的第一步。目标不可达时改为朝它方向走一步，绝不返回 WAIT。
std::optional<Intent> try_move_to(const DecisionContext& ctx, const Cell& goal,
                                  const std::string& reason, const BlockedFn& blocked,
                                  const CellSet& extra) {
  const Cell pos = ctx.state.position;
  if (goal == pos) {
    return std::nullopt;
  }
  const auto target = approach_cell(ctx, goal, blocked, extra);
  if (!target.has_value()) {
    return std::nullopt;
  }
  if (const auto step = bfs_first_step(pos, *target, ctx.state.map_width,
                                       ctx.state.map_height, blocked, extra);
      step.has_value()) {
    return Intent("move", *step, reason);
  }

  // 目标被阻挡无法到达时，至少沿曼哈顿距离靠近，保证 Tick 不被浪费
  std::optional<std::string> best_direction;
  int best_distance = manhattan(pos, *target);
  for (const auto& direction : kDirections) {
    const Cell cell = move_cell(pos, direction.name);
    if (!legal_cell(ctx, cell, blocked, extra)) {
      continue;
    }
    const int distance = manhattan(cell, *target);
    if (distance < best_distance) {
      best_distance = distance;
      best_direction = std::string(direction.name);
    }
  }
  if (best_direction.has_value()) {
    return Intent("move", *best_direction, reason + "_greedy");
  }
  return std::nullopt;
}

/// 最后兜底：任意可通行邻格（优先非风险格），避免原地 WAIT。
std::optional<Intent> last_resort_move(const DecisionContext& ctx, const CellSet& extra) {
  const Cell pos = ctx.state.position;
  std::optional<std::string> risky_direction;
  for (const auto& direction : kDirections) {
    const Cell cell = move_cell(pos, direction.name);
    if (!ctx.grid.in_bounds(cell) || ctx.grid.is_blocked(cell, ctx.tick)) {
      continue;
    }
    if (ctx.events.is_cell_blocked(cell, ctx.tick)) {
      continue;
    }
    if (extra.count(cell) > 0) {
      continue;
    }
    if (!ctx.events.is_cell_risky(cell, ctx.tick, ctx.state.hp)) {
      return Intent("move", std::string(direction.name), "stage2_step");
    }
    if (!risky_direction.has_value()) {
      risky_direction = std::string(direction.name);
    }
  }
  if (risky_direction.has_value()) {
    return Intent("move", *risky_direction, "stage2_step_risky");
  }
  return std::nullopt;
}

/// 资源候选：视野内的资源由跟踪器按“看得见才存在”即时增删，
/// 视野外保留记忆但设年龄上限；只有再次看到敌人在资源旁时才判定为已被抢。
std::vector<ResourceTrack> available_resources(const DecisionContext& ctx) {
  const auto remembered = ctx.tracker.remembered_enemies(ctx.tick, kContestedEnemyTicks);
  std::vector<ResourceTrack> result;
  for (const auto& track : ctx.tracker.active_resources(ctx.tick)) {
    if (ctx.tick - track.last_seen_tick > kResourceStaleTicks) {
      continue;
    }
    // 本 Tick 仍可见说明资源还在；只有离开视野后才考虑被对手取走
    bool contested = false;
    if (track.last_seen_tick < ctx.tick) {
      for (const auto& enemy : remembered) {
        if (manhattan(enemy.position, track.position) <= 1) {
          contested = true;
          break;
        }
      }
    }
    if (!contested) {
      result.push_back(track);
    }
  }
  return result;
}

/// 挑选一个能攻击到敌人的落点：优先敌人横纵方向 2 格，其次 1 格。
std::optional<Cell> attack_position(const DecisionContext& ctx, const EnemyTrack& enemy,
                                    const CellIntMap& distances, const BlockedFn& blocked,
                                    const CellSet& extra) {
  std::optional<Cell> best;
  int best_distance = std::numeric_limits<int>::max();
  int best_priority = std::numeric_limits<int>::max();
  for (const auto& direction : kDirections) {
    for (int distance = 2; distance >= 1; --distance) {
      const Cell cell = cell_in_direction(enemy.position, direction.name, distance);
      if (!ctx.grid.in_bounds(cell) || cell == ctx.state.position) {
        continue;
      }
      if (blocked(cell) || extra.count(cell) > 0) {
        continue;
      }
      const auto found = distances.find(cell);
      if (found == distances.end()) {
        continue;
      }
      const int priority = distance == 2 ? 0 : 1;
      const bool better = !best.has_value() || priority < best_priority ||
                          (priority == best_priority && found->second < best_distance) ||
                          (priority == best_priority && found->second == best_distance &&
                           cell < *best);
      if (better) {
        best = cell;
        best_distance = found->second;
        best_priority = priority;
      }
    }
  }
  return best;
}

/// 朝目标靠近的第一步；目标格被敌人占住时改走它边上最近的可通行格。
std::optional<std::string> step_toward(const DecisionContext& ctx, const Cell& goal,
                                       const CellIntMap& distances, const BlockedFn& blocked,
                                       const CellSet& extra) {
  const Cell pos = ctx.state.position;
  const auto step = bfs_first_step(pos, goal, ctx.state.map_width, ctx.state.map_height,
                                   blocked, extra);
  if (step.has_value()) {
    return step;
  }

  std::optional<Cell> best_cell;
  int best_distance = std::numeric_limits<int>::max();
  for (const auto& direction : kDirections) {
    const Cell cell = move_cell(goal, direction.name);
    if (!ctx.grid.in_bounds(cell) || cell == pos || blocked(cell) || extra.count(cell) > 0) {
      continue;
    }
    const auto found = distances.find(cell);
    if (found == distances.end() || found->second >= best_distance) {
      continue;
    }
    best_cell = cell;
    best_distance = found->second;
  }
  if (!best_cell.has_value()) {
    return std::nullopt;
  }
  return bfs_first_step(pos, *best_cell, ctx.state.map_width, ctx.state.map_height, blocked,
                        extra);
}

/// 追击：先接近当前可见敌人；allow_memory 时才追最近见过的敌人位置。
/// 早期不追记忆点，避免为了旧目标放弃资源搜索。
std::optional<Intent> chase_step(const DecisionContext& ctx,
                                 const std::vector<EnemyTrack>& enemies,
                                 const BlockedFn& blocked, const CellSet& extra,
                                 bool allow_memory) {
  const Cell pos = ctx.state.position;
  const CellIntMap distances =
      bfs_distances(pos, ctx.state.map_width, ctx.state.map_height, blocked, extra);

  std::vector<const EnemyTrack*> visible;
  for (const auto& enemy : enemies) {
    if (enemy.hp > 0 && !enemy.protected_) {
      visible.push_back(&enemy);
    }
  }
  std::sort(visible.begin(), visible.end(),
            [&pos](const EnemyTrack* lhs, const EnemyTrack* rhs) {
              return manhattan(pos, lhs->position) < manhattan(pos, rhs->position);
            });

  for (const auto* enemy : visible) {
    if (const auto target = attack_position(ctx, *enemy, distances, blocked, extra);
        target.has_value()) {
      const auto step = bfs_first_step(pos, *target, ctx.state.map_width, ctx.state.map_height,
                                       blocked, extra);
      if (step.has_value()) {
        return Intent("move", step, "stage2_hunt:" + enemy->robot_id);
      }
    }
    if (const auto step = step_toward(ctx, enemy->position, distances, blocked, extra);
        step.has_value()) {
      return Intent("move", step, "stage2_hunt:" + enemy->robot_id);
    }
  }

  const int memory = std::max(1, ctx.planning.hunt_memory_ticks);
  if (!allow_memory) {
    return std::nullopt;
  }
  const auto remembered = ctx.tracker.remembered_enemies(ctx.tick, memory);
  for (const auto& enemy : remembered) {
    const bool still_visible =
        std::any_of(enemies.begin(), enemies.end(), [&enemy](const EnemyTrack& visible) {
          return visible.robot_id == enemy.robot_id;
        });
    if (!still_visible && (enemy.position == pos || manhattan(pos, enemy.position) <= 1)) {
      // 到了旧位置仍未遇见：目标已离开，丢弃记忆，避免下一步又被旧目标拉回
      ctx.tracker.forget_enemy(enemy.robot_id);
      continue;
    }
    if (const auto step = step_toward(ctx, enemy.position, distances, blocked, extra);
        step.has_value()) {
      return Intent("move", step, "stage2_hunt_last_seen:" + enemy.robot_id);
    }
  }
  return std::nullopt;
}

/// 本 Tick 能在横纵直线 1~max_range 格内攻击到自己的敌人数（含保护中的敌人）。
int line_attackers(const Cell& pos, const std::vector<EnemyTrack>& enemies, int max_range) {
  int count = 0;
  for (const auto& enemy : enemies) {
    if (enemy.hp <= 0) {
      continue;
    }
    const int dx = std::abs(enemy.position.first - pos.first);
    const int dy = std::abs(enemy.position.second - pos.second);
    if ((dx == 0 && dy >= 1 && dy <= max_range) ||
        (dy == 0 && dx >= 1 && dx <= max_range)) {
      ++count;
    }
  }
  return count;
}

/// 低血时先尝试拉开距离，找不到更大的间距再原地防御。
std::optional<Intent> retreat_step(const DecisionContext& ctx,
                                   const std::vector<EnemyTrack>& enemies,
                                   const BlockedFn& blocked, const CellSet& extra) {
  const Cell pos = ctx.state.position;
  auto gap = [&enemies](const Cell& cell) {
    int best = std::numeric_limits<int>::max();
    for (const auto& enemy : enemies) {
      if (enemy.hp <= 0) {
        continue;
      }
      best = std::min(best, chebyshev(cell, enemy.position));
    }
    return best;
  };
  const int current = gap(pos);
  std::optional<std::string> best_direction;
  std::optional<Cell> best_cell;
  int best_gap = current;
  for (const auto& direction : kDirections) {
    const Cell cell = move_cell(pos, direction.name);
    if (!ctx.grid.in_bounds(cell) || blocked(cell) || extra.count(cell) > 0) {
      continue;
    }
    const int candidate = gap(cell);
    if (candidate > best_gap) {
      best_gap = candidate;
      best_direction = std::string(direction.name);
      best_cell = cell;
    }
  }
  if (!best_direction.has_value() || !best_cell.has_value()) {
    return std::nullopt;
  }
  return Intent("move", *best_direction, "stage2_retreat");
}

/// 与给定方向相反的方向，用于识别原路折返。
std::string opposite_direction(const std::string& direction) {
  if (direction == "up") return "down";
  if (direction == "down") return "up";
  if (direction == "left") return "right";
  if (direction == "right") return "left";
  return "";
}

/// 无敌人、无可收集资源时的巡逻：轮流前往中心与四个象限，保证持续移动。
/// 每步都必须缩短到当前航点的 BFS 距离，并在两 Tick 内避免原路折返，
/// 否则决策会与追击交替选中相反方向，导致相邻两格之间来回振荡。
std::optional<Intent> patrol_step(const DecisionContext& ctx, const BlockedFn& blocked,
                                  const CellSet& extra) {
  const int width = ctx.state.map_width;
  const int height = ctx.state.map_height;
  const Cell pos = ctx.state.position;
  const std::vector<Cell> waypoints{
      {width / 2, height / 2},
      {width / 4, height / 4},
      {width / 4, height * 3 / 4},
      {width * 3 / 4, height / 4},
      {width * 3 / 4, height * 3 / 4},
  };
  const auto last = ctx.events.last_move_direction(ctx.tick);
  const std::string reverse = last.has_value() ? opposite_direction(*last) : std::string();

  // 始终前往当前最远的航点：路程单调缩短，天然形成跨图巡回，
  // 不会像按 Tick 轮换航点那样在两点之间来回拉扯
  const CellIntMap from_pos = bfs_distances(pos, width, height, blocked, extra);
  std::optional<Cell> target;
  int farthest = -1;
  for (const auto& waypoint : waypoints) {
    if (!ctx.grid.in_bounds(waypoint) || waypoint == pos || blocked(waypoint)) {
      continue;
    }
    const auto found = from_pos.find(waypoint);
    if (found == from_pos.end() || found->second == 0) {
      continue;
    }
    if (found->second > farthest) {
      farthest = found->second;
      target = waypoint;
    }
  }
  if (!target.has_value()) {
    return std::nullopt;
  }

  const CellIntMap distances = bfs_distances(*target, width, height, blocked, extra);
  const auto here = distances.find(pos);
  if (here == distances.end() || here->second == 0) {
    return std::nullopt;
  }
  std::optional<std::string> forward;
  std::optional<std::string> backward;
  for (const auto& direction : kDirections) {
    const Cell cell = move_cell(pos, direction.name);
    if (!legal_cell(ctx, cell, blocked, extra)) {
      continue;
    }
    const auto found = distances.find(cell);
    if (found == distances.end() || found->second != here->second - 1) {
      continue;
    }
    if (!reverse.empty() && direction.name == reverse) {
      backward = std::string(direction.name);
    } else {
      forward = std::string(direction.name);
      break;
    }
  }
  if (forward.has_value()) {
    return Intent("move", *forward, "stage2_patrol");
  }
  if (backward.has_value()) {
    return Intent("move", *backward, "stage2_patrol");
  }
  return std::nullopt;
}

/// 天下无双增益下，ATTACK 命中自身横纵 ±2 格（5×5）内全部敌人，方向不限制目标集合。
std::optional<Intent> duel_attack(const DecisionContext& ctx,
                                  const std::vector<EnemyTrack>& enemies, bool buff_ready) {
  if (!buff_ready || !ctx.events.fortress_center(ctx.tick).has_value() ||
      !ctx.state.can_attack(ctx.stage)) {
    return std::nullopt;
  }
  const EnemyTrack* best = nullptr;
  for (const auto& enemy : enemies) {
    if (enemy.hp <= 0 || enemy.protected_) {
      continue;
    }
    if (chebyshev(enemy.position, ctx.state.position) > 2) {
      continue;
    }
    if (best == nullptr || enemy.hp < best->hp) {
      best = &enemy;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  return Intent("attack", direction_toward(ctx.state.position, best->position),
                "stage2_duel_attack:" + best->robot_id);
}

}  // namespace

namespace {
Intent decide_stage2(const DecisionContext& ctx);
}  // namespace

Intent stage2_decide(const DecisionContext& ctx) {
  const Intent intent = decide_stage2(ctx);
  if (intent.action == "move" && intent.direction.has_value()) {
    ctx.events.note_move(ctx.tick, *intent.direction);
  }
  return intent;
}

namespace {

/// 第二阶段战术决策本体；外层包装只负责记录最近移动方向。
Intent decide_stage2(const DecisionContext& ctx) {
  const StateStore& state = ctx.state;
  if (!state.alive) {
    return Intent("wait", std::nullopt, "stage2_dead");
  }

  const int tick = ctx.tick;
  const double hp = state.hp;
  const Cell pos = state.position;
  const int max_range = ctx.planning.attack_max_range;

  // LLM 只给高层策略，本地把它折算成有限的时间窗与阈值偏置，超出窗口自动失效
  const std::string strategy = ctx.events.strategy_hint(tick);
  const bool llm_fight = strategy == "fight";
  const bool llm_collect = strategy == "collect";
  const bool llm_reach = strategy == "reach";
  const bool llm_defend = strategy == "defend" || strategy == "avoid";
  const bool llm_heal = strategy == "heal";
  const bool llm_fortress = strategy == "fortress";

  const double low_hp = ctx.planning.low_hp_threshold;
  const double heal_hp =
      ctx.planning.heal_hp_threshold + (llm_heal ? kLlmHealBonus : 0.0);
  const int hunt_start =
      std::max(0, ctx.planning.hunt_after_tick - (llm_fight ? kLlmEarlyHunt : 0) +
                      (llm_collect ? kLlmLateHunt : 0));
  const bool late_phase = tick >= hunt_start;
  const int late_pickup = llm_collect ? kLlmLatePickupDistance : kLatePickupDistance;

  double defend_hp = ctx.planning.defend_hp_threshold + (llm_defend ? kLlmDefendBonus : 0.0);
  const bool vulnerable = ctx.events.vulnerability_active(tick);
  if (vulnerable) {
    defend_hp += 15;
  }

  const auto enemies = ctx.tracker.visible_enemies(ctx.perception);
  // 撤离判断使用“视野内 + 最近见过”的敌人：只靠当前视野会在视距边界来回折返
  std::vector<EnemyTrack> threat_source = enemies;
  for (const auto& remembered : ctx.tracker.remembered_enemies(tick, kThreatMemoryTicks)) {
    const bool still_visible =
        std::any_of(enemies.begin(), enemies.end(), [&remembered](const EnemyTrack& enemy) {
          return enemy.robot_id == remembered.robot_id;
        });
    if (!still_visible) {
      threat_source.push_back(remembered);
    }
  }
  const auto threat = enemies_within(pos, threat_source, 2);
  const bool risky_here = ctx.events.is_cell_risky(pos, tick, hp);

  // 堡垒驻留必须在任何分支返回前更新，否则连续计数会被打断。
  const auto fortress = ctx.events.fortress_center(tick);
  const bool at_fortress = fortress.has_value() && ctx.events.inside_fortress(pos, tick);
  if (fortress.has_value()) {
    ctx.events.note_fortress_hold(tick, at_fortress);
  }
  const bool buff_ready =
      ctx.events.fortress_buff_ready(ctx.planning.fortress_stay_ticks);

  const BlockedFn blocked = [&ctx, tick, hp](const Cell& cell) {
    if (ctx.grid.is_blocked(cell, tick)) {
      return true;
    }
    if (ctx.events.is_cell_blocked(cell, tick)) {
      return true;
    }
    return ctx.events.is_cell_risky(cell, tick, hp);
  };

  CellSet extra;
  for (const auto& enemy : enemies) {
    extra.insert(enemy.position);
  }

  // 已处于即将激活的危险区域时，优先向安全邻格撤离
  if (risky_here) {
    for (const auto& direction : kDirections) {
      const Cell cell = move_cell(pos, direction.name);
      if (legal_cell(ctx, cell, blocked, extra)) {
        return Intent("move", std::string(direction.name), "stage2_escape_hazard");
      }
    }
    if (state.can_defend(ctx.stage)) {
      return Intent("defend", std::nullopt, "stage2_hazard_defend");
    }
    if (const auto step = last_resort_move(ctx, extra); step.has_value()) {
      return *step;
    }
    return Intent("defend", std::nullopt, "stage2_hazard_blocked");
  }

  // 1. 天下无双：已取得增益后优先用 5×5 攻击
  if (const auto duel = duel_attack(ctx, enemies, buff_ready); duel.has_value()) {
    return *duel;
  }

  // 2. 攻击：斩杀必打；后期血量尚可持续输出；前期只处理贴脸威胁
  const auto attack = best_attack(pos, enemies, tick, &ctx.events, max_range);
  const int attackers = line_attackers(pos, enemies, max_range);
  if (attack.has_value() && state.can_attack(ctx.stage)) {
    const int damage =
        normal_damage(attack->target.defending, attack->target.protected_, vulnerable);
    if (attack->target.hp <= static_cast<double>(damage)) {
      return Intent("attack", attack->direction,
                    "stage2_attack:" + attack->target.robot_id);
    }
  }
  // 被两路以上同时锁定时，低血先防御把本 Tick 伤害减半，避免做亏本换血
  if (attackers >= 2 && hp <= defend_hp + 15.0 && state.can_defend(ctx.stage)) {
    return Intent("defend", std::nullopt, "stage2_defend_focus");
  }
  if (attack.has_value() && state.can_attack(ctx.stage)) {
    const bool healthy = hp > (late_phase ? defend_hp : kEarlyTradeHp);
    const bool late_aggressive = late_phase && hp > low_hp;
    const bool early_self_defense = !late_phase && attack->distance <= 1;
    if (late_aggressive || (healthy && early_self_defense)) {
      return Intent("attack", attack->direction,
                    "stage2_attack:" + attack->target.robot_id);
    }
  }

  // 3. 低血生存：攻击先于移动结算；已在射程内时换血比后撤更划算
  //    （后撤这 Tick 仍会吃满同一次伤害，还丢掉一次输出）
  if (hp <= defend_hp && !threat.empty()) {
    const bool can_defend = state.can_defend(ctx.stage);
    if (attackers > 0) {
      const double hit = 20.0 * attackers;
      const double hit_defending = 10.0 * attackers;
      const bool lethal_standing = hp <= hit;
      const bool lethal_defending = hp <= hit_defending;
      if (attack.has_value() && state.can_attack(ctx.stage)) {
        const int damage =
            normal_damage(attack->target.defending, attack->target.protected_, vulnerable);
        if (attack->target.hp <= static_cast<double>(damage) || !lethal_standing) {
          return Intent("attack", attack->direction,
                        "stage2_attack:" + attack->target.robot_id);
        }
      }
      // 不防御会死、防御能活下来时，防御严格优于本 Tick 还要吃满伤害的移动
      if (can_defend && lethal_standing && !lethal_defending) {
        return Intent("defend", std::nullopt, "stage2_defend_lethal");
      }
      // 能持续输出且不会立刻阵亡时，保持防御姿态减半伤害，下一 Tick 继续打
      if (can_defend && !lethal_standing) {
        return Intent("defend", std::nullopt, "stage2_defend_in_line");
      }
    }
    if (const auto retreat = retreat_step(ctx, threat, blocked, extra);
        retreat.has_value()) {
      return *retreat;
    }
    if (can_defend) {
      return Intent("defend", std::nullopt,
                    attackers > 0 ? "stage2_defend_in_line" : "stage2_defend_low_hp");
    }
  }

  if (hp < heal_hp) {
    const auto heal_point = ctx.events.healing_point(pos, tick, hp);
    if (heal_point.has_value() && *heal_point != pos) {
      if (const auto step = try_move_to(ctx, *heal_point, "stage2_heal", blocked, extra);
          step.has_value()) {
        return *step;
      }
    }
  }

  // 4. 资源与任务点：前期资源第一，后期任务点先补齐
  const auto resources = available_resources(ctx);
  std::string prefer = "any";
  if (ctx.missions.needs("high_resources")) {
    prefer = "high";
  } else if (ctx.missions.needs("normal_resources")) {
    prefer = "normal";
  }
  const auto choice = pick_resource(pos, resources, state.map_width, state.map_height,
                                    blocked, prefer, extra, true);

  const auto target = reach_target(ctx);
  const bool reach_now = target.has_value() && ctx.missions.needs("reach") &&
                         *target != pos;

  if (!late_phase) {
    // 前期：资源优先；到 reach_priority_tick 后先去任务点，只在顺路时捡资源
    const bool reach_soon = reach_now &&
                            (tick >= ctx.planning.reach_priority_tick || llm_reach);
    const bool cheap_resource = choice.has_value() && choice->step.has_value() &&
                                choice->distance <= kCheapResourceDistance;
    if (reach_soon && (!cheap_resource || llm_reach)) {
      if (const auto move = try_move_to(ctx, *target, "stage2_reach", blocked, extra);
          move.has_value()) {
        return *move;
      }
    }
    // LLM 明确要求抢堡垒时，先拿天下无双，但仍顺手收 4 格内资源
    const bool fortress_now = llm_fortress && fortress.has_value() && !buff_ready &&
                              threat.empty() &&
                              (!choice.has_value() || !choice->step.has_value() ||
                               choice->distance > kCheapResourceDistance);
    if (fortress_now) {
      if (at_fortress) {
        return Intent("defend", std::nullopt, "stage2_fortress_hold");
      }
      if (pos != *fortress) {
        if (const auto move = try_move_to(ctx, *fortress, "stage2_fortress", blocked, extra);
            move.has_value()) {
          return *move;
        }
      }
    }
    if (choice.has_value() && choice->step.has_value()) {
      return Intent("move", choice->step, "stage2_resource:" + choice->resource_id);
    }
    if (reach_now) {
      if (const auto move = try_move_to(ctx, *target, "stage2_reach", blocked, extra);
          move.has_value()) {
        return *move;
      }
    }
    // 未取得增益且没有资源可抢时才去堡垒驻留
    if (fortress.has_value() && !buff_ready) {
      if (at_fortress) {
        return Intent("defend", std::nullopt, "stage2_fortress_hold");
      }
      if (pos != *fortress) {
        if (const auto move = try_move_to(ctx, *fortress, "stage2_fortress", blocked, extra);
            move.has_value()) {
          return *move;
        }
      }
    }
    // LLM 要求输出时优先接近敌人，而不是先把探索走完；残血时不主动接敌
    if (llm_fight && hp > defend_hp) {
      if (const auto chase = chase_step(ctx, enemies, blocked, extra, true);
          chase.has_value()) {
        return *chase;
      }
    }
    if (ctx.planning.exploration_enabled) {
      const auto frontier = pick_frontier(pos, ctx.grid.seen, state.map_width,
                                          state.map_height, blocked, extra);
      if (frontier.has_value() && frontier->second.has_value()) {
        return Intent("move", frontier->second, "stage2_explore");
      }
    }
    // 早期不再主动追击视野内的敌人：资源与跨图巡回收益更高，
    // 敌人进入射程时上面的攻击分支已经会处理
  } else {
    // 后期：先拿天下无双增益，再任务点，然后主动找人输出
    if (fortress.has_value() && !buff_ready) {
      if (at_fortress) {
        return Intent("defend", std::nullopt, "stage2_fortress_hold");
      }
      if (threat.empty() && pos != *fortress) {
        if (const auto move = try_move_to(ctx, *fortress, "stage2_fortress", blocked, extra);
            move.has_value()) {
          return *move;
        }
      }
    }
    if (reach_now) {
      if (const auto move = try_move_to(ctx, *target, "stage2_reach", blocked, extra);
          move.has_value()) {
        return *move;
      }
    }
    // 顺路资源（近距离）仍要捡，资源价值是同分时的第一优先比较项
    if (choice.has_value() && choice->step.has_value() &&
        choice->distance <= late_pickup) {
      return Intent("move", choice->step, "stage2_resource:" + choice->resource_id);
    }
    // 残血时以保命为先，不再主动追击
    if (hp > defend_hp) {
      if (const auto chase = chase_step(ctx, enemies, blocked, extra, !llm_defend);
          chase.has_value()) {
        return *chase;
      }
    }
    if (choice.has_value() && choice->step.has_value()) {
      return Intent("move", choice->step, "stage2_resource:" + choice->resource_id);
    }
    if (ctx.planning.exploration_enabled) {
      const auto frontier = pick_frontier(pos, ctx.grid.seen, state.map_width,
                                          state.map_height, blocked, extra);
      if (frontier.has_value() && frontier->second.has_value()) {
        return Intent("move", frontier->second, "stage2_explore");
      }
    }
  }

  if (const auto patrol = patrol_step(ctx, blocked, extra); patrol.has_value()) {
    return *patrol;
  }
  // 任何情况都不空转：实在无路可走才尝试兜底移动或防御
  if (const auto step = last_resort_move(ctx, extra); step.has_value()) {
    return *step;
  }
  if (state.can_defend(ctx.stage)) {
    return Intent("defend", std::nullopt, "stage2_hold");
  }
  return Intent("wait", std::nullopt, "stage2_dead_end");
}

}  // namespace

}  // namespace rmtc_bot
