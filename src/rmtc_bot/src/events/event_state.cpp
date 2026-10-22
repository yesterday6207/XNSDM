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

bool EventState::is_cell_risky(const Cell& cell, int tick, double hp) const {
  for (const auto& event : events) {
    if (event.type == "explosion") {
      if (!event.active_from.has_value() || !event.active_until.has_value()) {
        continue;
      }
      const int delta = *event.active_from - tick;
      if (!(0 <= delta && delta <= 2)) {
        continue;
      }
      if (*event.active_until <= tick) {
        continue;
      }
      if (hp <= 80 && area_contains(event.area, cell)) {
        return true;
      }
    }
    if (event.type == "world_split") {
      if (!event.point.has_value() || !event.active_from.has_value()) {
        continue;
      }
      if (tick + 1 == *event.active_from &&
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

std::string EventState::describe() const {
  std::ostringstream oss;
  oss << "strategy=" << strategy << " tick=" << strategy_tick
      << " events=" << events.size() << " summary=" << summary.substr(0, 120);
  return oss.str();
}

}  // namespace rmtc_bot
