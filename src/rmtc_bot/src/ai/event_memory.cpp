#include "rmtc_bot/ai/event_memory.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace

EventMemory::EventMemory(std::size_t max_items) : max_items_(max_items) { reset(); }

void EventMemory::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  ordered_.clear();
  pending_.clear();
}

std::vector<std::string> EventMemory::ingest_text(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  std::vector<std::string> lines;
  for (const auto& line : split_lines(text)) {
    const std::string cleaned = trim(line);
    if (!cleaned.empty()) {
      lines.push_back(cleaned);
    }
  }
  return ingest_many(lines);
}

std::vector<std::string> EventMemory::ingest_many(const std::vector<std::string>& texts) {
  std::vector<std::string> added;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& text : texts) {
    const std::string cleaned = trim(text);
    if (cleaned.empty()) {
      continue;
    }
    if (std::find(ordered_.begin(), ordered_.end(), cleaned) != ordered_.end()) {
      continue;
    }
    ordered_.push_back(cleaned);
    pending_.push_back(cleaned);
    added.push_back(cleaned);
  }
  if (ordered_.size() > max_items_) {
    const std::size_t overflow = ordered_.size() - max_items_;
    std::vector<std::string> removed(ordered_.begin(),
                                     ordered_.begin() + static_cast<std::ptrdiff_t>(overflow));
    ordered_.erase(ordered_.begin(), ordered_.begin() + static_cast<std::ptrdiff_t>(overflow));
    for (const auto& item : removed) {
      const auto pending_it = std::find(pending_.begin(), pending_.end(), item);
      if (pending_it != pending_.end()) {
        pending_.erase(pending_it);
      }
    }
  }
  return added;
}

std::vector<std::string> EventMemory::pending_snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_;
}

void EventMemory::ack_pending(const std::vector<std::string>& items) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& item : items) {
    const auto it = std::find(pending_.begin(), pending_.end(), item);
    if (it != pending_.end()) {
      pending_.erase(it);
    }
  }
}

std::string EventMemory::recent_text(int max_chars) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> chunks;
  int total = 0;
  for (auto it = ordered_.rbegin(); it != ordered_.rend(); ++it) {
    const int length = static_cast<int>(it->size());
    if (total + length + 1 > max_chars) {
      break;
    }
    chunks.push_back(*it);
    total += length + 1;
  }
  std::reverse(chunks.begin(), chunks.end());
  std::ostringstream oss;
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    if (i > 0) {
      oss << "\n";
    }
    oss << chunks[i];
  }
  return oss.str();
}

EventMemory::Stats EventMemory::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  Stats result;
  result.seen = ordered_.size();
  result.pending = pending_.size();
  result.ordered = ordered_.size();
  return result;
}

}  // namespace rmtc_bot
