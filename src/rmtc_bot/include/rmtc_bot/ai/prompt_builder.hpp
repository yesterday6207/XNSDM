#pragma once

#include <json/json.h>

#include <string>
#include <vector>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

/// 赛前封存的系统提示词，现场不得修改。
extern const char* const kSystemPrompt;

std::string build_user_prompt(const Perception& perception,
                              const std::vector<std::string>& event_texts,
                              const std::string& recent_event_text,
                              int max_event_chars = 6000);

Json::Value build_messages(const Perception& perception,
                           const std::vector<std::string>& event_texts,
                           const std::string& recent_event_text,
                           int max_event_chars = 6000);

}  // namespace rmtc_bot
