#pragma once

#include <optional>
#include <string>
#include <vector>

#include "rmtc_bot/ai/output_validator.hpp"
#include "rmtc_bot/planning/grid_map.hpp"
#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

struct TimedEvent {
  std::string type;
  std::optional<int> active_from;
  std::optional<int> active_until;
  std::optional<Area> area;
  std::optional<Cell> point;
  std::string recommendation;
};

/// 由校验后的 LLM 高层结论维护事件生效窗口与规划提示。
class EventState {
 public:
  EventState();

  void reset();
  void apply(const LLMDecision& decision, int now_tick, GridMap* grid_map);
  void purge(int tick);

  bool clean_reflect_active(int tick) const;
  bool illusion_active(int tick) const;
  bool vulnerability_active(int tick) const;
  bool is_cell_blocked(const Cell& cell, int tick) const;
  bool is_cell_risky(const Cell& cell, int tick, double hp) const;
  std::optional<Cell> healing_point(const Cell& self_pos, int tick, double hp) const;
  std::optional<Cell> fortress_center(int tick) const;
  /// 该格是否位于当前生效的天下无双堡垒（3×3）内。
  bool inside_fortress(const Cell& cell, int tick) const;
  /// 记录本 Tick 是否位于堡垒内，用于判断连续驻留是否已满 required_ticks。
  void note_fortress_hold(int tick, bool at_center);
  /// 连续驻留达到 required_ticks 后视为已获得天下无双增益（本地推断）。
  bool fortress_buff_ready(int required_ticks = 10) const;
  void reset_fortress_hold();
  std::string strategy_hint(int tick) const;
  /// 记录最近一次实际发出的移动方向，用于避免相邻两格之间来回振荡。
  void note_move(int tick, const std::string& direction);
  std::optional<std::string> last_move_direction(int tick, int max_age = 2) const;
  std::string describe() const;

  std::vector<TimedEvent> events;
  std::string strategy = "explore";
  int strategy_tick = -1;
  std::string summary;

 private:
  int fortress_hold_ticks_ = 0;
  int fortress_last_tick_ = -1;
  int move_tick_ = -1;
  std::string move_direction_;
};

}  // namespace rmtc_bot
