#pragma once

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include <json/json.h>

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "rmtc_bot/action/action_sender.hpp"
#include "rmtc_bot/ai/event_memory.hpp"
#include "rmtc_bot/ai/llm_client.hpp"
#include "rmtc_bot/ai/quota_manager.hpp"
#include "rmtc_bot/decision/decision_router.hpp"
#include "rmtc_bot/decision/safety_guard.hpp"
#include "rmtc_bot/events/event_input.hpp"
#include "rmtc_bot/events/event_state.hpp"
#include "rmtc_bot/events/mission_input.hpp"
#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/perception/parser.hpp"
#include "rmtc_bot/perception/state_store.hpp"
#include "rmtc_bot/planning/grid_map.hpp"
#include "rmtc_bot/registration/team_registry.hpp"
#include "rmtc_bot/utils/config_loader.hpp"
#include "rmtc_bot/utils/logger.hpp"

namespace rmtc_bot {

struct AiResult {
  std::string session_id;
  int tick = 0;
  std::string content;
  std::vector<std::string> pending;
};

class RmtcBotNode : public rclcpp::Node {
 public:
  RmtcBotNode();
  ~RmtcBotNode() override;

  void shutdown_log();

 private:
  struct AiWorker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };

  void on_perception(const std_msgs::msg::String::SharedPtr msg);
  void on_mission(const std_msgs::msg::String::SharedPtr msg);
  void on_event(const std_msgs::msg::String::SharedPtr msg);

  void start_session(const Perception& perception);
  void handle_last_action(const Perception& perception);
  void reload_decision_type(bool force = false);
  void maybe_request_ai(const Perception& perception);
  void ai_worker(const std::string& session_id, int tick, const Json::Value& messages,
                 const std::vector<std::string>& pending);
  void drain_ai_results(const Perception& perception);
  void reap_workers();

  Logger logger_;
  std::string team_;
  std::string team_name_;
  int stage_ = 2;
  std::filesystem::path config_dir_;
  Params params_;

  std::filesystem::path decision_config_path_;
  std::filesystem::path decision_enum_path_;
  std::optional<std::filesystem::file_time_type> decision_mtime_;
  std::string decision_type_ = "local_fallback";

  std::string token_;
  std::unique_ptr<LLMClient> llm_;
  bool ai_disabled_logged_ = false;
  QuotaManager quota_;

  StateStore state_;
  EntityTracker tracker_;
  GridMap grid_;
  EventMemory event_memory_;
  EventInput event_input_;
  MissionInput missions_;
  EventState events_;
  DecisionRouter router_;
  std::unique_ptr<SafetyGuard> guard_;

  rclcpp::QoS sensor_qos_;
  rclcpp::QoS latched_qos_;
  rclcpp::QoS event_qos_;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_publisher_;
  std::unique_ptr<ActionSender> sender_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr perception_subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr mission_subscription_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr event_subscription_;
  std::unique_ptr<TeamRegistry> registry_;

  std::optional<std::pair<int, Cell>> last_move_attempt_;

  std::mutex results_mutex_;
  std::deque<AiResult> ai_results_;
  std::vector<AiWorker> workers_;
  std::atomic<bool> shutting_down_{false};
};

}  // namespace rmtc_bot
