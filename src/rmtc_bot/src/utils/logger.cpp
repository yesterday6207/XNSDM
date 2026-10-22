#include "rmtc_bot/utils/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace rmtc_bot {

std::mutex Logger::write_mutex_;
std::mutex Logger::secrets_mutex_;
std::vector<std::string> Logger::secrets_;

namespace {

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "INFO";
}

std::string timestamp() {
  std::time_t now = std::time(nullptr);
  std::tm tm_buf{};
  localtime_r(&now, &tm_buf);
  char buffer[32] = {0};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
  return buffer;
}

}  // namespace

Logger::Logger(std::string name) : name_(std::move(name)) {}

void Logger::register_secret(const std::string& value) {
  if (value.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(secrets_mutex_);
  for (const auto& secret : secrets_) {
    if (secret == value) {
      return;
    }
  }
  secrets_.push_back(value);
}

std::string Logger::redact(const std::string& text) {
  std::string result = text;
  const char* token = std::getenv("RMTC_TEAM_TOKEN");
  if (token != nullptr && token[0] != '\0') {
    const std::string secret(token);
    std::string::size_type pos = 0;
    while ((pos = result.find(secret, pos)) != std::string::npos) {
      result.replace(pos, secret.size(), "***");
      pos += 3;
    }
  }
  {
    std::lock_guard<std::mutex> lock(secrets_mutex_);
    for (const auto& secret : secrets_) {
      if (secret.empty()) {
        continue;
      }
      std::string::size_type pos = 0;
      while ((pos = result.find(secret, pos)) != std::string::npos) {
        result.replace(pos, secret.size(), "***");
        pos += 3;
      }
    }
  }
  return result;
}

void Logger::write(LogLevel level, const std::string& message) const {
  const std::string line = redact(message);
  std::lock_guard<std::mutex> lock(write_mutex_);
  std::fprintf(stderr, "%s %s %s: %s\n", timestamp().c_str(), level_name(level),
               name_.c_str(), line.c_str());
  std::fflush(stderr);
}

void Logger::debug(const std::string& message) const { write(LogLevel::kDebug, message); }
void Logger::info(const std::string& message) const { write(LogLevel::kInfo, message); }
void Logger::warn(const std::string& message) const { write(LogLevel::kWarn, message); }
void Logger::error(const std::string& message) const { write(LogLevel::kError, message); }

}  // namespace rmtc_bot
