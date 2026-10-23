#include "rmtc_bot/events/event_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace rmtc_bot {

namespace {

bool in_window(const TimedEvent& event, int tick) {
  if (!event.active_from.has_value() || !event.active_until.has_value()) {
    return false;
  }
  return *event.active_from <= tick && tick < *event.active_until;
}

bool area_contains(const std::optional<Area>& area, const Cell& cell) {
  if (!area.has_value()) {
    return false;
  }
  return area->x1 <= cell.first && cell.first <= area->x2 && area->y1 <= cell.second &&
         cell.second <= area->y2;
}

/// 世界分裂激活前多少 Tick 就视为整行整列危险；留出走到线外的时间。
constexpr int kWorldSplitEscapeTicks = 5;

/// 潘潘的神触发窗口占事件时长的比例；正式规则为前 20/30 Tick。
constexpr int kPanpanTriggerNumerator = 2;
constexpr int kPanpanTriggerDenominator = 3;

/// 事件前段“移动即获得易伤”的截止 Tick（不含）。
int panpan_move_window_end(const TimedEvent& event) {
  if (!event.active_from.has_value() || !event.active_until.has_value()) {
    return -1;
  }
  const int duration = std::max(1, *event.active_until - *event.active_from);
  return *event.active_from + std::max(
      1, duration * kPanpanTriggerNumerator / kPanpanTriggerDenominator);
}

bool same_event(const TimedEvent& lhs, const TimedEvent& rhs) {
  return lhs.type == rhs.type && lhs.active_from == rhs.active_from &&
         lhs.active_until == rhs.active_until && lhs.area == rhs.area &&
         lhs.point == rhs.point;
}

}  // namespace

EventState::EventState() { reset(); }

void EventState::reset() {
  events.clear();
  strategy = "explore";
  strategy_tick = -1;
  summary.clear();
  fortress_hold_ticks_ = 0;
  fortress_last_tick_ = -1;
  move_tick_ = -1;
  move_direction_.clear();
  vulnerable_from_ = -1;
  vulnerable_until_ = -1;
  visited_.clear();
  hit_count_ = 0;
  attack_tick_ = -1;
  observed_tick_ = -1;
  default_reach_latched_ = false;
  chase_target_id_.clear();
  chase_tick_ = -1;
  patrol_target_ = Cell{0, 0};
  patrol_tick_ = -1;
  last_alive_ = false;
  has_alive_sample_ = false;
  enemy_hp_.clear();
}

void EventState::apply(const LLMDecision& decision, int now_tick, GridMap* grid_map) {
  for (const auto& incoming : decision.events) {
    if (incoming.active_until.has_value() && *incoming.active_until <= now_tick) {
      continue;
    }
    TimedEvent event;
    event.type = incoming.type;
    event.active_from = incoming.active_from;
    event.active_until = incoming.active_until;
    event.area = incoming.area;
    event.point = incoming.point;
    event.recommendation = incoming.recommendation;

    bool duplicate = false;
    for (const auto& existing : events) {
      if (same_event(existing, event)) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    events.push_back(event);

    if (event.type == "duel") {
      // 新堡垒出现：上一轮的驻留进度与增益均已失效
      reset_fortress_hold();
    }

    if (event.type == "world_split" && event.point.has_value() && grid_map != nullptr) {
      const int start = event.active_from.value_or(now_tick);
      const int end = event.active_until.value_or(start + 30);
      // 提前一 Tick 将该十字线视为不可进入，避免激活瞬间站在线上
      grid_map->add_temp_wall_cross(event.point->first, event.point->second,
                                    std::max(0, start - 1), end, "world_split");
    }
  }
  if (!decision.strategy.empty()) {
    strategy = decision.strategy;
  }
  strategy_tick = now_tick;
  if (!decision.summary.empty()) {
    summary = decision.summary;
  }
  purge(now_tick);
}

void EventState::purge(int tick) {
  events.erase(std::remove_if(events.begin(), events.end(),
                              [tick](const TimedEvent& event) {
                                return event.active_until.has_value() &&
                                       *event.active_until <= tick;
                              }),
               events.end());
}

bool EventState::clean_reflect_active(int tick) const {
  for (const auto& event : events) {
    if (event.type == "clean" && in_window(event, tick)) {
      return true;
    }
  }
  return false;
}

bool EventState::illusion_active(int tick) const {
  for (const auto& event : events) {
    if (event.type == "illusion" && in_window(event, tick)) {
      return true;
    }
  }
  return false;
}

bool EventState::vulnerability_active(int tick) const {
  for (const auto& event : events) {
    if (event.type == "panpan" && in_window(event, tick)) {
      return true;
    }
  }
  return false;
}

bool EventState::self_vulnerable(int tick) const {
  return vulnerable_until_ >= 0 && vulnerable_from_ >= 0 && tick >= vulnerable_from_ &&
         tick < vulnerable_until_;
}

bool EventState::moved_in_panpan_window(int move_tick, int tick) const {
  if (move_tick < 0 || move_tick >= tick) {
    return false;
  }
  for (const auto& event : events) {
    if (event.type != "panpan" || !event.active_from.has_value() ||
        !event.active_until.has_value()) {
      continue;
    }
    if (tick >= *event.active_until) {
      continue;
    }
    const int window_end = panpan_move_window_end(event);
    if (window_end >= 0 && move_tick >= *event.active_from && move_tick < window_end) {
      return true;
    }
  }
  return false;
}

bool EventState::is_cell_blocked(const Cell& cell, int tick) const {
  for (const auto& event : events) {
    if (event.type != "world_split" || !event.point.has_value()) {
      continue;
    }
    if (!event.active_from.has_value() || !event.active_until.has_value()) {
      continue;
    }
    if (!(*event.active_from - 1 <= tick && tick < *event.active_until)) {
      continue;
    }
    if (cell.first == event.point->first || cell.second == event.point->second) {
      return true;
    }
  }
  return false;
}

bool EventState::is_cell_risky(const Cell& cell, int tick, bool protected_self) const {
  // 出生保护免疫爆炸/辣椒等环境伤害，此时不必为回避危险格放弃输出机会
  const bool environment_safe = protected_self;
  for (const auto& event : events) {
    if (event.type == "explosion") {
      if (!event.active_from.has_value() || !event.active_until.has_value()) {
        continue;
      }
      if (environment_safe) {
        continue;
      }
      const int delta = *event.active_from - tick;
      if (!(0 <= delta && delta <= 2)) {
        continue;
      }
      if (*event.active_until <= tick) {
        continue;
      }
      // 正式规则爆炸一次 80 HP 且无视防御：任何血量进入矩形都是重亏，一律避让
      if (area_contains(event.area, cell)) {
        return true;
      }
    }
    if (event.type == "world_split") {
      if (!event.point.has_value() || !event.active_from.has_value()) {
        continue;
      }
      // 世界分裂会直接击毁线上的战车：提前离开整行整列。
      // 走到线外最多需要数格，只提前 2 Tick 经常来不及，因此放宽到 5 Tick。
      const int delta = *event.active_from - tick;
      if (0 <= delta && delta <= kWorldSplitEscapeTicks &&
          (cell.first == event.point->first || cell.second == event.point->second)) {
        return true;
      }
    }
    if (event.type == "pepper") {
      if (event.point.has_value() && in_window(event, tick) && cell == *event.point) {
        return true;
      }
    }
  }
  return false;
}

std::optional<Cell> EventState::healing_point(const Cell& self_pos, int tick, double hp) const {
  if (hp >= 100) {
    return std::nullopt;
  }
  std::optional<Cell> best;
  int best_distance = 0;
  for (const auto& event : events) {
    if (event.type != "angel" || !event.point.has_value()) {
      continue;
    }
    if (!in_window(event, tick)) {
      continue;
    }
    const int distance = std::abs(event.point->first - self_pos.first) +
                         std::abs(event.point->second - self_pos.second);
    const bool better = !best.has_value() || distance < best_distance ||
                        (distance == best_distance && *event.point < *best);
    if (better) {
      best = event.point;
      best_distance = distance;
    }
  }
  return best;
}

std::optional<Cell> EventState::fortress_center(int tick) const {
  for (const auto& event : events) {
    if (event.type == "duel" && event.area.has_value() && in_window(event, tick)) {
      return Cell{(event.area->x1 + event.area->x2) / 2,
                  (event.area->y1 + event.area->y2) / 2};
    }
  }
  return std::nullopt;
}

bool EventState::inside_fortress(const Cell& cell, int tick) const {
  for (const auto& event : events) {
    if (event.type == "duel" && event.area.has_value() && in_window(event, tick)) {
      return area_contains(event.area, cell);
    }
  }
  return false;
}

void EventState::note_fortress_hold(int tick, bool at_center) {
  if (!at_center) {
    fortress_hold_ticks_ = 0;
    fortress_last_tick_ = tick;
    return;
  }
  if (tick == fortress_last_tick_) {
    return;  // 同一 Tick 重复调用不重复计数
  }
  if (fortress_last_tick_ >= 0 && tick == fortress_last_tick_ + 1) {
    ++fortress_hold_ticks_;
  } else {
    fortress_hold_ticks_ = 1;
  }
  fortress_last_tick_ = tick;
}

bool EventState::fortress_buff_ready(int required_ticks) const {
  return fortress_hold_ticks_ >= std::max(1, required_ticks);
}

void EventState::reset_fortress_hold() {
  fortress_hold_ticks_ = 0;
  fortress_last_tick_ = -1;
}

std::string EventState::strategy_hint(int tick) const {
  if (strategy_tick >= 0 && tick - strategy_tick <= 100) {
    return strategy;
  }
  return "explore";
}

void EventState::note_move(int tick, const std::string& direction) {
  move_tick_ = tick;
  move_direction_ = direction;
  // 潘潘的神：触发窗口内的成功移动会在下一 Tick 获得易伤，持续到事件统一到期。
  for (const auto& event : events) {
    if (event.type != "panpan" || !event.active_until.has_value()) {
      continue;
    }
    const int window_end = panpan_move_window_end(event);
    if (window_end < 0 || tick < *event.active_from || tick >= window_end) {
      continue;
    }
    vulnerable_from_ = tick + 1;
    vulnerable_until_ = *event.active_until;
  }
}

std::optional<std::string> EventState::last_move_direction(int tick, int max_age) const {
  if (move_tick_ < 0 || move_direction_.empty()) {
    return std::nullopt;
  }
  if (tick - move_tick_ > max_age) {
    return std::nullopt;
  }
  return move_direction_;
}

void EventState::note_visited(int tick, const Cell& cell) {
  if (!visited_.empty() && visited_.back().first == tick && visited_.back().second == cell) {
    return;
  }
  visited_.emplace_back(tick, cell);
  // 只保留最近一段时间的轨迹，避免状态无界增长
  constexpr int kKeepTicks = 64;
  visited_.erase(std::remove_if(visited_.begin(), visited_.end(),
                                [tick](const std::pair<int, Cell>& entry) {
                                  return tick - entry.first > kKeepTicks;
                                }),
                 visited_.end());
}

void EventState::note_chase_target(const std::string& robot_id, int tick) {
  chase_target_id_ = robot_id;
  chase_tick_ = tick;
}

std::optional<std::string> EventState::chase_target(int tick, int max_age) const {
  if (chase_tick_ < 0 || chase_target_id_.empty() || tick - chase_tick_ > max_age) {
    return std::nullopt;
  }
  return chase_target_id_;
}

void EventState::note_patrol_target(const Cell& cell, int tick) {
  patrol_target_ = cell;
  patrol_tick_ = tick;
}

std::optional<Cell> EventState::patrol_target(int tick, int max_age) const {
  if (patrol_tick_ < 0 || tick - patrol_tick_ > max_age) {
    return std::nullopt;
  }
  return patrol_target_;
}

void EventState::note_attack(int tick) { attack_tick_ = tick; }

void EventState::observe_combat(
    int tick, bool alive, const std::vector<std::pair<std::string, double>>& enemies) {
  // 同一 Tick 只结算一次，重发/重复决策不会重复计数
  if (observed_tick_ == tick) {
    return;
  }
  observed_tick_ = tick;

  if (has_alive_sample_ && last_alive_ && !alive) {
    hit_count_ = std::max(0, hit_count_ - kDeathHitPenalty);
  }
  last_alive_ = alive;
  has_alive_sample_ = true;

  // 攻击在当 Tick 的战斗阶段结算，下一 Tick 才能在感知里看到目标掉血；
  // 留一格余量吸收感知延迟，同一次掉血不会被重复统计（第二次差值已为 0）。
  const bool attacked_recently = attack_tick_ >= 0 && tick - attack_tick_ >= 1 &&
                                 tick - attack_tick_ <= 2;
  if (attacked_recently) {
    for (const auto& entry : enemies) {
      const auto previous = enemy_hp_.find(entry.first);
      if (previous != enemy_hp_.end() && previous->second > entry.second) {
        hit_count_ += 1;
      }
    }
  }

  enemy_hp_.clear();
  for (const auto& entry : enemies) {
    enemy_hp_[entry.first] = entry.second;
  }
}

bool EventState::recently_visited(const Cell& cell, int tick, int max_age) const {
  return std::any_of(visited_.begin(), visited_.end(),
                     [&cell, tick, max_age](const std::pair<int, Cell>& entry) {
                       return entry.second == cell && tick - entry.first <= max_age;
                     });
}

std::string EventState::describe() const {
  std::ostringstream oss;
  oss << "strategy=" << strategy << " tick=" << strategy_tick
      << " events=" << events.size() << " summary=" << summary.substr(0, 120);
  return oss.str();
}

}  // namespace rmtc_bot
