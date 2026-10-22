#include "rmtc_bot/events/event_input.hpp"

#include <cctype>

namespace rmtc_bot {

namespace {

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

}  // namespace

std::vector<std::string> extract_announcements(const std::string& text) {
  std::vector<std::string> result;
  if (text.empty()) {
    return result;
  }
  std::string current;
  for (const char ch : text) {
    if (ch == '\n') {
      const std::string cleaned = trim(current);
      if (!cleaned.empty()) {
        result.push_back(cleaned);
      }
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  const std::string cleaned = trim(current);
  if (!cleaned.empty()) {
    result.push_back(cleaned);
  }
  return result;
}

EventInput::EventInput(EventMemory& memory) : memory_(memory) {}

std::vector<std::string> EventInput::from_ros_text(const std::string& text) {
  return memory_.ingest_text(text);
}

std::vector<std::string> EventInput::from_perception(const Perception& perception) {
  return memory_.ingest_many(extract_announcements(perception.event_text));
}

}  // namespace rmtc_bot
