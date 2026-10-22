#pragma once

#include <string>
#include <vector>

#include "rmtc_bot/ai/event_memory.hpp"
#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

/// 只做缓存、去重与时效管理，不解析事件语义。
class EventInput {
 public:
  explicit EventInput(EventMemory& memory);

  std::vector<std::string> from_ros_text(const std::string& text);
  std::vector<std::string> from_perception(const Perception& perception);

 private:
  EventMemory& memory_;
};

std::vector<std::string> extract_announcements(const std::string& text);

}  // namespace rmtc_bot
