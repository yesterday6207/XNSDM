#pragma once

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <string>
#include <unordered_map>

#include "rmtc_bot/utils/logger.hpp"

namespace rmtc_bot {

/// 队名清洗与校验：UTF-8 长度 1..96 字节。
bool validate_team_name(const std::string& name, std::string& cleaned, std::string& error);

/// 队伍注册发布与队伍名录订阅。
class TeamRegistry {
 public:
  TeamRegistry(rclcpp::Node* node, const std::string& team, const std::string& name,
               const rclcpp::QoS& qos, Logger* logger);

  void announce();
  std::unordered_map<std::string, std::string> mapping() const;

 private:
  void on_registry(const std_msgs::msg::String::SharedPtr msg);

  rclcpp::Node* node_;
  Logger* logger_;
  std::string team_;
  std::string name_;
  std::unordered_map<std::string, std::string> mapping_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
};

}  // namespace rmtc_bot
