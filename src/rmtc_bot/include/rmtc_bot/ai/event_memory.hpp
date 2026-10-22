#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace rmtc_bot {

/// 公告缓存、按内容去重与场次切换清空。
class EventMemory {
 public:
  struct Stats {
    std::size_t seen = 0;
    std::size_t pending = 0;
    std::size_t ordered = 0;
  };

  explicit EventMemory(std::size_t max_items = 128);

  void reset();
  std::vector<std::string> ingest_text(const std::string& text);
  std::vector<std::string> ingest_many(const std::vector<std::string>& texts);
  std::vector<std::string> pending_snapshot() const;
  void ack_pending(const std::vector<std::string>& items);
  std::string recent_text(int max_chars = 6000) const;
  Stats stats() const;

 private:
  std::size_t max_items_;
  mutable std::mutex mutex_;
  std::vector<std::string> ordered_;
  std::vector<std::string> pending_;
};

}  // namespace rmtc_bot
