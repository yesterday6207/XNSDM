#pragma once

#include <optional>
#include <string>

namespace rmtc_bot {

struct Action {
  int tick = 0;
  std::string session_id;
  std::string action;
  std::optional<std::string> direction;
};

/// 动作字段必须小写，单条消息不超过 4096 字节。
bool validate_action(const Action& action, std::string& error);

bool build_action(int tick, const std::string& session_id, const std::string& action,
                  const std::optional<std::string>& direction, Action& out,
                  std::string& error);

bool serialize_action(const Action& action, std::string& out, std::string& error);

/// 解析并校验动作 JSON；未知字段会被拒绝（用于外部输入与测试）。
bool parse_action_json(const std::string& text, Action& out, std::string& error);

}  // namespace rmtc_bot
