#include "rmtc_bot/ai/prompt_builder.hpp"

#include <cctype>
#include <sstream>

#include "rmtc_bot/utils/logger.hpp"

namespace rmtc_bot {

const char* const kSystemPrompt =
    "你是 RMTC 机甲对抗赛参赛机器人的高层战术分析器。\n"
    "你的唯一职责是理解自然语言随机事件公告与公开任务，并输出可校验的高层判断。\n"
    "严格约束：\n"
    "1. 只输出一个 JSON 对象，不要输出 Markdown、解释文字或多个 JSON。\n"
    "2. 禁止输出任何逐 Tick 动作，禁止出现 action、direction、moves 等字段。\n"
    "3. 事件类型只能是：explosion, illusion, angel, duel, world_split, pepper, clean, "
    "panpan, other。\n"
    "4. strategy 只能是：collect, reach, fight, defend, explore, heal, fortress, "
    "avoid。\n"
    "5. recommendation 只能是：avoid, enter, defend, attack, ignore, fortress, "
    "heal。\n"
    "6. active_from/active_until 使用公告中的逻辑 Tick 绝对值；无法确定时写 null。\n"
    "7. area 使用绝对坐标 [x1,y1,x2,y2]；point 使用绝对坐标 [x,y]；无则为 null。\n"
    "8. 公告未明确给出参数时不要臆造坐标或时间，宁可写 null。\n"
    "输出格式：\n"
    "{\"strategy\":\"collect\",\"summary\":\"不超过200字\",\"events\":[{\"type\":"
    "\"explosion\",\"active_from\":75,\"active_until\":75,\"area\":[4,4,9,9],"
    "\"point\":null,\"recommendation\":\"avoid\"}]}";

namespace {

std::string point_text(const Cell& cell) {
  std::ostringstream oss;
  oss << "(" << cell.first << ", " << cell.second << ")";
  return oss.str();
}

std::string number_text(double value) {
  std::ostringstream oss;
  oss << value;
  return oss.str();
}

std::string optional_text(const std::optional<double>& value) {
  return value.has_value() ? number_text(*value) : "None";
}

std::string optional_point(const std::optional<Cell>& value) {
  return value.has_value() ? point_text(*value) : "None";
}

std::string entity_summary(const Perception& perception) {
  std::vector<std::string> enemies;
  std::vector<std::string> resources;
  for (const auto& entity : perception.entities) {
    if (entity.type == "robot") {
      enemies.push_back(cat(entity.robot_id.value_or(""), "@", point_text(entity.position),
                            " hp=", optional_text(entity.hp),
                            " def=", entity.defending ? "True" : "False",
                            " prot=", entity.protected_ ? "True" : "False"));
    } else if (entity.type == "resource_normal" || entity.type == "resource_high") {
      std::ostringstream value_text;
      if (entity.value.has_value()) {
        value_text << *entity.value;
      } else {
        value_text << "None";
      }
      resources.push_back(cat(entity.id, "@", point_text(entity.position),
                             " value=", value_text.str()));
    }
  }
  std::vector<std::string> parts;
  if (!enemies.empty()) {
    std::ostringstream oss;
    oss << "可见敌人: ";
    for (std::size_t i = 0; i < enemies.size(); ++i) {
      if (i > 0) {
        oss << "; ";
      }
      oss << enemies[i];
    }
    parts.push_back(oss.str());
  }
  if (!resources.empty()) {
    std::ostringstream oss;
    oss << "可见资源: ";
    for (std::size_t i = 0; i < resources.size(); ++i) {
      if (i > 0) {
        oss << "; ";
      }
      oss << resources[i];
    }
    parts.push_back(oss.str());
  }
  if (parts.empty()) {
    return "视野内无敌人或资源";
  }
  std::ostringstream oss;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      oss << "\n";
    }
    oss << parts[i];
  }
  return oss.str();
}

std::string join_lines(const std::vector<std::string>& items) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      oss << "\n";
    }
    oss << items[i];
  }
  return oss.str();
}

}  // namespace

std::string build_user_prompt(const Perception& perception,
                              const std::vector<std::string>& event_texts,
                              const std::string& recent_event_text, int max_event_chars) {
  std::string event_blob = join_lines(event_texts);
  const std::string trimmed = event_blob;
  // 去掉首尾空白，与 Python strip 一致
  std::size_t begin = 0;
  std::size_t end = trimmed.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(trimmed[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(trimmed[end - 1])) != 0) {
    --end;
  }
  event_blob = trimmed.substr(begin, end - begin);
  if (static_cast<int>(event_blob.size()) > max_event_chars && max_event_chars > 0) {
    event_blob = event_blob.substr(event_blob.size() -
                                   static_cast<std::size_t>(max_event_chars));
  }

  std::string recent_blob = recent_event_text;
  if (static_cast<int>(recent_blob.size()) > max_event_chars && max_event_chars > 0) {
    recent_blob = recent_blob.substr(recent_blob.size() -
                                     static_cast<std::size_t>(max_event_chars));
  }

  std::vector<std::string> missions;
  for (const auto& mission : perception.missions) {
    missions.push_back(cat(mission.id, ":", mission.type,
                           " target=", optional_text(mission.target),
                           " position=", optional_point(mission.position)));
  }

  std::ostringstream score_ids;
  score_ids << "[";
  for (std::size_t i = 0; i < perception.score.mission_completed_ids.size(); ++i) {
    if (i > 0) {
      score_ids << ", ";
    }
    score_ids << "'" << perception.score.mission_completed_ids[i] << "'";
  }
  score_ids << "]";

  std::ostringstream oss;
  oss << "当前场次: " << perception.session_id << "\n"
      << "当前 Tick: " << perception.tick << "\n"
      << "地图尺寸: " << perception.map_width << "x" << perception.map_height << "\n"
      << "本车: team=" << perception.self.team << " pos=" << point_text(perception.self.position)
      << " hp=" << number_text(perception.self.hp)
      << " alive=" << (perception.self.alive ? "True" : "False")
      << " protected=" << (perception.self.protected_ ? "True" : "False")
      << " defending=" << (perception.self.defending ? "True" : "False") << "\n"
      << "比分: resource_value=" << number_text(perception.score.resource_value)
      << " effective_damage=" << number_text(perception.score.effective_damage)
      << " mission_completed=" << number_text(perception.score.mission_completed)
      << " completed_ids=" << score_ids.str() << "\n"
      << "任务文本: " << perception.mission_text << "\n"
      << "结构化任务: [" << join_lines(missions) << "]\n"
      << entity_summary(perception) << "\n\n"
      << "本次新公告（必须理解）:\n"
      << (event_blob.empty() ? "（无新公告）" : event_blob) << "\n\n"
      << "近期公告缓存:\n"
      << (recent_blob.empty() ? "（无）" : recent_blob) << "\n\n"
      << "请依据上述公告和任务，输出规定格式的高层 JSON。";
  return oss.str();
}

Json::Value build_messages(const Perception& perception,
                           const std::vector<std::string>& event_texts,
                           const std::string& recent_event_text, int max_event_chars) {
  Json::Value messages(Json::arrayValue);
  Json::Value system_message(Json::objectValue);
  system_message["role"] = "system";
  system_message["content"] = kSystemPrompt;
  messages.append(system_message);

  Json::Value user_message(Json::objectValue);
  user_message["role"] = "user";
  user_message["content"] =
      build_user_prompt(perception, event_texts, recent_event_text, max_event_chars);
  messages.append(user_message);
  return messages;
}

}  // namespace rmtc_bot
