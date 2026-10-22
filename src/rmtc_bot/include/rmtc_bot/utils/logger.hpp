#pragma once

#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace rmtc_bot {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

/// 简易拼接工具：cat("tick=", 3, " pos=", cell)。
template <typename... Args>
std::string cat(Args&&... args) {
  std::ostringstream oss;
  (oss << ... << args);
  return oss.str();
}

/// 脱敏日志：任何路径都不允许把 RMTC_TEAM_TOKEN 写入输出。
class Logger {
 public:
  explicit Logger(std::string name);

  void debug(const std::string& message) const;
  void info(const std::string& message) const;
  void warn(const std::string& message) const;
  void error(const std::string& message) const;

  static void register_secret(const std::string& value);
  static std::string redact(const std::string& text);

 private:
  void write(LogLevel level, const std::string& message) const;

  std::string name_;
  static std::mutex write_mutex_;
  static std::mutex secrets_mutex_;
  static std::vector<std::string> secrets_;
};

}  // namespace rmtc_bot
