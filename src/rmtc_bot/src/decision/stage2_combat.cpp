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
/// 前期换血门槛：血量还扛得住一次交火就主动接战，宁打不绕。
constexpr double kEarlyTradeHp = 50.0;
/// 撤离时把最近见过的敌人也当作威胁，避免离开视野后立刻折返。
constexpr int kThreatMemoryTicks = 6;
/// 普通攻击单次伤害；潘潘的神易伤期间翻倍，防御时减半。
constexpr double kNormalHit = 20.0;
constexpr double kVulnerableHit = 40.0;
/// 巡逻避开最近该 Tick 数内走过的格，防止在几格之间循环空耗输出机会。
constexpr int kPatrolTabuTicks = 24;
/// 巡逻航点粘滞：锁定一个航点后沿路程单调缩短地走，到达或超时才换下一个。
constexpr int kPatrolWaypointTicks = 40;
/// 资源计分满分门槛：资源价值达到该值后继续采集不再加分。
constexpr double kResourceScoreFullValue = 120.0;
/// 保守期把防御阈值抬高，让车更早转入防御/撤离而不是继续换血。
constexpr double kConservativeDefendBonus = 15.0;

/// 到达任务点。协议给出明确坐标时严格按坐标执行；
/// 只有自然语言任务、使用参配默认到达点时放宽为“相邻即算到达并锁定”，
/// 否则车会为了踩默认点（常常正好是场地中心十字线）反复折返，正中世界分裂的火线。
std::optional<Cell> reach_target(const DecisionContext& ctx) {
  const auto target = ctx.missions.reach_target(ctx.planning.stage2_reach_target);
  if (!target.has_value()) {
    return target;
  }
  const bool has_explicit_position =
      std::any_of(ctx.missions.missions.begin(), ctx.missions.missions.end(),
                  [](const Mission& mission) {
                    return mission.type == "reach" && mission.position.has_value();
                  });
  if (has_explicit_position) {
    return target;
  }
  if (ctx.events.default_reach_latched()) {
    return std::nullopt;
  }
  const Cell pos = ctx.state.position;
  if (std::abs(pos.first - target->first) <= 1 && std::abs(pos.second - target->second) <= 1) {
    ctx.events.latch_default_reach();
    return std::nullopt;
  }
  return target;
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
    if (!ctx.events.is_cell_risky(cell, ctx.tick, ctx.state.protected_)) {
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

/// 敌人是否处于潘潘的神易伤：事件触发窗口内观察到它成功移动过。
bool enemy_vulnerable(const DecisionContext& ctx, const EnemyTrack& enemy) {
  return ctx.events.moved_in_panpan_window(enemy.last_move_tick, ctx.tick);
}

/// 一次普通攻击对该敌人的实际伤害。
int damage_against(const DecisionContext& ctx, const EnemyTrack& target) {
  return normal_damage(target.defending, target.protected_,
                       enemy_vulnerable(ctx, target));
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
  // 击杀奖励 60 相当于三次普通命中，优先追击一次即可击毁的目标
  std::sort(visible.begin(), visible.end(),
            [&pos, &ctx](const EnemyTrack* lhs, const EnemyTrack* rhs) {
              const bool lhs_kill = lhs->hp <= static_cast<double>(damage_against(ctx, *lhs));
              const bool rhs_kill = rhs->hp <= static_cast<double>(damage_against(ctx, *rhs));
              if (lhs_kill != rhs_kill) {
                return lhs_kill;
              }
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
  // 旧记忆的平均遍历顺序不稳定：多个目标交替选中会让车在两格之间来回抖动，
  // 因此先粘住上一 Tick 的追击目标，直到记忆过期或到达仍未见人再换。
  const auto latched = ctx.events.chase_target(ctx.tick);
  const auto remembered = ctx.tracker.remembered_enemies(ctx.tick, memory);
  std::vector<const EnemyTrack*> ordered;
  for (const auto& enemy : remembered) {
    const bool still_visible =
        std::any_of(enemies.begin(), enemies.end(), [&enemy](const EnemyTrack& visible) {
          return visible.robot_id == enemy.robot_id;
        });
    if (!still_visible) {
      ordered.push_back(&enemy);
    }
  }
  std::stable_sort(ordered.begin(), ordered.end(),
                   [&latched](const EnemyTrack* lhs, const EnemyTrack* rhs) {
                     const bool lhs_latched = latched.has_value() && lhs->robot_id == *latched;
                     const bool rhs_latched = latched.has_value() && rhs->robot_id == *latched;
                     return lhs_latched && !rhs_latched;
                   });
  for (const auto* enemy : ordered) {
    if (enemy->position == pos || manhattan(pos, enemy->position) <= 1) {
      // 到了旧位置仍未遇见：目标已离开，丢弃记忆，避免下一步又被旧目标拉回
      ctx.tracker.forget_enemy(enemy->robot_id);
      continue;
    }
    if (const auto step = step_toward(ctx, enemy->position, distances, blocked, extra);
        step.has_value()) {
      ctx.events.note_chase_target(enemy->robot_id, ctx.tick);
      return Intent("move", step, "stage2_hunt_last_seen:" + enemy->robot_id);
    }
  }
  return std::nullopt;
}

/// 本 Tick 能在横纵直线 1~max_range 格内攻击到自己的敌人数。
/// 出生保护中的敌人按规则不能 ATTACK，不计入威胁。
int line_attackers(const Cell& pos, const std::vector<EnemyTrack>& enemies, int max_range) {
  int count = 0;
  for (const auto& enemy : enemies) {
    if (enemy.hp <= 0 || enemy.protected_) {
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

/// 无敌人、无可收集资源时的巡逻：锁定一个象限航点后一路走过去，到达/超时才换。
/// 不把场地中心当航点：中心正是世界分裂十字线交点，停留越久越容易被秒杀。
/// 航点必须粘滞：每 Tick 重新挑“最远航点”会在两个对角之间翻转，
/// 车会在 (9,9)→(9,8)→(8,8)→(8,9) 这样的四格里死循环（即“场地中间转圈”）。
/// 每步都必须缩短到当前航点的 BFS 距离，并在两 Tick 内避免原路折返，
/// 否则决策会与追击交替选中相反方向，导致相邻两格之间来回振荡。
std::optional<Intent> patrol_step(const DecisionContext& ctx, const BlockedFn& blocked,
                                  const CellSet& extra) {
  const int width = ctx.state.map_width;
  const int height = ctx.state.map_height;
  const Cell pos = ctx.state.position;
  const std::vector<Cell> waypoints{
      {width / 4, height / 4},
      {width / 4, height * 3 / 4},
      {width * 3 / 4, height / 4},
      {width * 3 / 4, height * 3 / 4},
  };
  const auto last = ctx.events.last_move_direction(ctx.tick);
  const std::string reverse = last.has_value() ? opposite_direction(*last) : std::string();

  const CellIntMap from_pos = bfs_distances(pos, width, height, blocked, extra);
  std::optional<Cell> target;
  // 先沿用已锁定的航点，保证路程单调缩短；只有它已到达、超时或变得不可达才重挑
  if (const auto latched = ctx.events.patrol_target(ctx.tick, kPatrolWaypointTicks);
      latched.has_value() && *latched != pos && !blocked(*latched)) {
    const auto found = from_pos.find(*latched);
    if (found != from_pos.end() && found->second > 0) {
      target = *latched;
    }
  }
  if (!target.has_value()) {
    // 没有锁定航点时挑当前最远的象限航点，形成跨图巡回而不是原地震荡
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
  }
  ctx.events.note_patrol_target(*target, ctx.tick);

  const CellIntMap distances = bfs_distances(*target, width, height, blocked, extra);
  const auto here = distances.find(pos);
  if (here == distances.end() || here->second == 0) {
    return std::nullopt;
  }
  // 中心十字线（世界分裂的攻击线）上只做短暂穿越，不在其上停留或来回走
  const auto on_center_cross = [width, height](const Cell& cell) {
    return cell.first == width / 2 || cell.second == height / 2;
  };
  // 站在中心线上时优先横向离开：此处离任一角航点常只有沿线的方向能缩短距离，
  // 若坚持“每步都缩短距离”，车就会被锁在十字线上反复走位，正好撞进世界分裂
  if (on_center_cross(pos)) {
    for (const auto& direction : kDirections) {
      if (!reverse.empty() && direction.name == reverse) {
        continue;
      }
      const Cell cell = move_cell(pos, direction.name);
      if (on_center_cross(cell) || !legal_cell(ctx, cell, blocked, extra)) {
        continue;
      }
      if (ctx.events.recently_visited(cell, ctx.tick, kPatrolTabuTicks)) {
        continue;
      }
      return Intent("move", std::string(direction.name), "stage2_patrol");
    }
  }
  std::optional<std::string> forward;
  std::optional<std::string> fresh;   // 最近没走过的前进方向
  std::optional<std::string> fresh_off_cross;  // 既没走过又能离开中心线
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
      continue;
    }
    if (!forward.has_value()) {
      forward = std::string(direction.name);
    }
    if (ctx.events.recently_visited(cell, ctx.tick, kPatrolTabuTicks)) {
      continue;
    }
    fresh = std::string(direction.name);
    if (!on_center_cross(cell)) {
      fresh_off_cross = std::string(direction.name);
      break;
    }
  }
  // 优先没走过、且能离开中心十字线的格：只靠“禁止原路折返”仍会在四格之间循环
  if (fresh_off_cross.has_value()) {
    return Intent("move", *fresh_off_cross, "stage2_patrol");
  }
  if (fresh.has_value()) {
    return Intent("move", *fresh, "stage2_patrol");
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
  // 洁身自好：攻击防御中的敌人会反伤且无收益，无双攻击也应跳过该目标
  const bool reflect = ctx.events.clean_reflect_active(ctx.tick);
  const EnemyTrack* best = nullptr;
  for (const auto& enemy : enemies) {
    if (enemy.hp <= 0 || enemy.protected_) {
      continue;
    }
    if (reflect && enemy.defending) {
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
  // 先用本 Tick 感知结算上一 Tick 的攻击命中与自身死亡，供保守策略判断
  std::vector<std::pair<std::string, double>> visible;
  for (const auto& enemy : ctx.tracker.visible_enemies(ctx.perception)) {
    visible.emplace_back(enemy.robot_id, enemy.hp);
  }
  ctx.events.observe_combat(ctx.tick, ctx.perception.self.alive, visible);

  const Intent intent = decide_stage2(ctx);
  if (intent.action == "move" && intent.direction.has_value()) {
    ctx.events.note_move(ctx.tick, *intent.direction);
  }
  if (intent.action == "attack") {
    ctx.events.note_attack(ctx.tick);
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

  const double heal_hp =
      ctx.planning.heal_hp_threshold + (llm_heal ? kLlmHealBonus : 0.0);
  const int hunt_start =
      std::max(0, ctx.planning.hunt_after_tick - (llm_fight ? kLlmEarlyHunt : 0) +
                      (llm_collect ? kLlmLateHunt : 0));
  // 任务全部完成且资源分已拿满（资源计分门槛 120）后，经济收益归零，立即转入输出期：
  // 对抗分满分门槛为 500 有效伤害，早一步开打就多一点累计
  const bool missions_done = !ctx.missions.needs("normal_resources") &&
                             !ctx.missions.needs("high_resources") &&
                             !ctx.missions.needs("reach");
  const bool economy_done = missions_done &&
                            ctx.state.score.resource_value >= kResourceScoreFullValue;
  const bool late_phase = tick >= hunt_start || economy_done;
  const int late_pickup = llm_collect ? kLlmLatePickupDistance : kLatePickupDistance;

  // 累计命中达到 60 后进入保守期：有效伤害已经拿满对抗分，
  // 此后每死一次都会扣 60 并等待 3 Tick，继续换血只会回吐得分。
  const bool conservative = ctx.events.conservative_combat();
  double defend_hp = ctx.planning.defend_hp_threshold + (llm_defend ? kLlmDefendBonus : 0.0);
  if (conservative) {
    defend_hp += kConservativeDefendBonus;
  }
  // 潘潘的神易伤：自身受到普通攻击 40 HP、防御时 20 HP，需更早转入防御/撤离
  const bool self_vulnerable = ctx.events.self_vulnerable(tick);
  if (self_vulnerable) {
    defend_hp += 15;
  }
  const double incoming_hit = self_vulnerable ? kVulnerableHit : kNormalHit;
  const double incoming_defend = incoming_hit / 2.0;

  const auto enemies = ctx.tracker.visible_enemies(ctx.perception);
  // 撤离判断使用“视野内 + 最近见过”的敌人：只靠当前视野会在视距边界来回折返。
  // 出生保护中的敌人不能攻击，不构成威胁，避免为免伤误判而放弃输出。
  std::vector<EnemyTrack> threat_source;
  for (const auto& enemy : enemies) {
    if (!enemy.protected_) {
      threat_source.push_back(enemy);
    }
  }
  for (const auto& remembered : ctx.tracker.remembered_enemies(tick, kThreatMemoryTicks)) {
    if (remembered.protected_) {
      continue;
    }
    const bool still_visible =
        std::any_of(enemies.begin(), enemies.end(), [&remembered](const EnemyTrack& enemy) {
          return enemy.robot_id == remembered.robot_id;
        });
    if (!still_visible) {
      threat_source.push_back(remembered);
    }
  }
  const auto threat = enemies_within(pos, threat_source, 2);
  const bool risky_here = ctx.events.is_cell_risky(pos, tick, state.protected_);
  // 记录轨迹：巡逻时避开刚走过的格，防止在几格之间绕圈空耗 Tick
  ctx.events.note_visited(tick, pos);

  // 堡垒驻留必须在任何分支返回前更新，否则连续计数会被打断。
  const auto fortress = ctx.events.fortress_center(tick);
  const bool at_fortress = fortress.has_value() && ctx.events.inside_fortress(pos, tick);
  if (fortress.has_value()) {
    ctx.events.note_fortress_hold(tick, at_fortress);
  }
  const bool buff_ready =
      ctx.events.fortress_buff_ready(ctx.planning.fortress_stay_ticks);

  // 血量不健康时不再把落点选在敌人横纵射程内：进攻也要留活路，
  // 否则移动过去只是把下一次阵亡提前 60 点有效伤害。
  // 阈值下调为“再吃一次攻击就会低于防御线”，只在真正危险时让路，
  // 避免中血时全图都是禁区、被迫在场地中间绕圈。
  const bool avoid_enemy_line = hp <= defend_hp - incoming_hit;
  const BlockedFn blocked = [&ctx, tick, enemies, max_range, avoid_enemy_line](const Cell& cell) {
    if (ctx.grid.is_blocked(cell, tick)) {
      return true;
    }
    if (ctx.events.is_cell_blocked(cell, tick)) {
      return true;
    }
    if (ctx.events.is_cell_risky(cell, tick, ctx.state.protected_)) {
      return true;
    }
    if (!avoid_enemy_line) {
      return false;
    }
    for (const auto& enemy : enemies) {
      if (enemy.hp <= 0 || enemy.protected_) {
        continue;
      }
      const int dx = std::abs(enemy.position.first - cell.first);
      const int dy = std::abs(enemy.position.second - cell.second);
      if ((dx == 0 && dy >= 1 && dy <= max_range) ||
          (dy == 0 && dx >= 1 && dx <= max_range)) {
        return true;
      }
    }
    return false;
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

  // 2. 攻击：斩杀必打；后期血量尚可持续输出；前期只处理贴脸威胁。
  //    每次死亡扣 60 有效伤害并等待 3 Tick，因此不为一次击杀主动送命。
  const auto attack = best_attack(pos, enemies, tick, &ctx.events, max_range);
  const int attackers = line_attackers(pos, enemies, max_range);
  const bool can_defend = state.can_defend(ctx.stage);
  // 本 Tick 同线敌人的总伤害（易伤时每次 40，防御减半）
  const double hit = incoming_hit * std::max(1, attackers);
  const double hit_defending = incoming_defend * std::max(1, attackers);
  const bool lethal_standing = attackers > 0 && hp <= hit;
  const bool lethal_defending = attackers > 0 && hp <= hit_defending;
  if (attack.has_value() && state.can_attack(ctx.stage)) {
    const int damage = damage_against(ctx, attack->target);
    const bool kill = attack->target.hp <= static_cast<double>(damage);
    // 击杀值得打，但若站着必死而防御能活，就不要用一次击杀换掉复活等待
    if (kill && (!lethal_standing || !can_defend || lethal_defending)) {
      return Intent("attack", attack->direction,
                    "stage2_attack:" + attack->target.robot_id);
    }
  }
  // 被两路以上同时锁定时，低血先防御把本 Tick 伤害减半，避免做亏本换血
  if (attackers >= 2 && hp <= defend_hp + 15.0 && can_defend) {
    return Intent("defend", std::nullopt, "stage2_defend_focus");
  }
  if (attack.has_value() && state.can_attack(ctx.stage)) {
    // 保守期要求扛得住本 Tick 全部同线伤害才继续输出，不再只看单次攻击
    const bool safe_to_trade = conservative ? hp > defend_hp + hit : hp > incoming_hit;
    const bool healthy = hp > (late_phase ? defend_hp : kEarlyTradeHp);
    const bool late_aggressive = late_phase && safe_to_trade;
    const bool early_self_defense = !late_phase && attack->distance <= 1;
    if (late_aggressive || (healthy && early_self_defense)) {
      return Intent("attack", attack->direction,
                    "stage2_attack:" + attack->target.robot_id);
    }
  }

  // 3. 低血生存：攻击先于移动结算；已在射程内时换血比后撤更划算
  //    （后撤这 Tick 仍会吃满同一次伤害，还丢掉一次输出）
  if (hp <= defend_hp && !threat.empty()) {
    if (attackers > 0) {
      if (attack.has_value() && state.can_attack(ctx.stage)) {
        const int damage = damage_against(ctx, attack->target);
        const bool kill = attack->target.hp <= static_cast<double>(damage);
        // 不会被击毁就继续输出；必死且防御也救不回来时，用一次击杀换命
        if (!lethal_standing || (kill && (!can_defend || lethal_defending))) {
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
    // 没有资源可抢时先主动找敌人打，而不是把 Tick 花在跨图巡逻上；
    // 只有血量低于防御线、LLM 要求避战或已进入保守期（累计命中 ≥ 70）才放弃主动接敌。
    if (!conservative && !llm_defend && hp > defend_hp) {
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
    // 残血时以保命为先，不再主动追击；保守期只追视野内目标，不追旧记忆
    const double chase_hp = defend_hp + (conservative ? incoming_hit : 0.0);
    if (hp > chase_hp) {
      if (const auto chase = chase_step(ctx, enemies, blocked, extra,
                                        !llm_defend && !conservative);
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
