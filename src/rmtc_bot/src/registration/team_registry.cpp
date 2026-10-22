#include "rmtc_bot/registration/team_registry.hpp"

#include <json/json.h>

#include <cctype>
#include <functional>
#include <memory>

namespace rmtc_bot {

bool validate_team_name(const std::string& name, std::string& cleaned, std::string& error) {
  error.clear();
  cleaned.clear();
  for (const char ch : name) {
    if (ch == '\r' || ch == '\n') {
      continue;
    }
    cleaned.push_back(ch);
  }
  std::size_t begin = 0;
  std::size_t end = cleaned.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(cleaned[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(cleaned[end - 1])) != 0) {
    --end;
  }
  cleaned = cleaned.substr(begin, end - begin);
  if (cleaned.empty() || cleaned.size() > 96) {
    error = "队名 UTF-8 长度必须在 1..96 字节";
    return false;
  }
  return true;
}

TeamRegistry::TeamRegistry(rclcpp::Node* node, const std::string& team,
                           const std::string& name, const rclcpp::QoS& qos, Logger* logger)
    : node_(node), logger_(logger), team_(team) {
  std::string cleaned;
  std::string error;
  if (validate_team_name(name, cleaned, error)) {
    name_ = cleaned;
  } else {
    logger_->warn("队名不合法，使用默认名称: " + error);
    name_ = team + "-bot";
  }

  publisher_ = node_->create_publisher<std_msgs::msg::String>(
      "/team_" + team_ + "/registration", qos);
  subscription_ = node_->create_subscription<std_msgs::msg::String>(
      "/referee/team_registry", qos,
      std::bind(&TeamRegistry::on_registry, this, std::placeholders::_1));
}

void TeamRegistry::announce() {
  Json::Value payload(Json::objectValue);
  payload["team"] = team_;
  payload["name"] = name_;
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["emitUTF8"] = true;
  std_msgs::msg::String message;
  message.data = Json::writeString(builder, payload);
  publisher_->publish(message);
  logger_->info("已发布注册: team=" + team_ + " name=" + name_);
}

void TeamRegistry::on_registry(const std_msgs::msg::String::SharedPtr msg) {
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value data;
  std::string errors;
  if (!reader->parse(msg->data.data(), msg->data.data() + msg->data.size(), &data, &errors)) {
    logger_->warn("队伍名录不是合法 JSON");
    return;
  }
  if (!data.isObject() || !data["teams"].isArray()) {
    return;
  }
  for (const auto& item : data["teams"]) {
    if (!item.isObject()) {
      continue;
    }
    if (item["team"].isString() && item["name"].isString()) {
      mapping_[item["team"].asString()] = item["name"].asString();
    }
  }
}

std::unordered_map<std::string, std::string> TeamRegistry::mapping() const {
  return mapping_;
}

}  // namespace rmtc_bot
