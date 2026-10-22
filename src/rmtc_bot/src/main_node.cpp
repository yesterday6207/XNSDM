#include "rmtc_bot/main_node.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <sstream>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "rmtc_bot/action/action_schema.hpp"
#include "rmtc_bot/action/last_action.hpp"
#include "rmtc_bot/ai/output_validator.hpp"
#include "rmtc_bot/ai/prompt_builder.hpp"
#include "rmtc_bot/planning/path_planner.hpp"
#include "rmtc_bot/utils/time_window.hpp"

namespace rmtc_bot {

namespace {

std::string env_or(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  return value;
}

std::string trim_upper(std::string text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  text = text.substr(begin, end - begin);
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return text.empty() ? "A" : text;
}

int sanitize_stage(int stage) {
  if (stage == 0 || stage == 1 || stage == 2) {
    return stage;
  }
  return 2;
}

std::filesystem::path resolve_config_dir(const std::string& explicit_path) {
  std::error_code error;
  if (!explicit_path.empty()) {
    const std::filesystem::path candidate(explicit_path);
    if (std::filesystem::exists(candidate, error) &&
        std::filesystem::is_directory(candidate, error)) {
      return candidate;
    }
    return candidate;  // 由调用方记录不存在
  }
  try {
    const std::filesystem::path share(ament_index_cpp::get_package_share_directory("rmtc_bot"));
    const std::filesystem::path candidate = share / "config";
    if (std::filesystem::exists(candidate, error)) {
      return candidate;
    }
  } catch (const std::exception&) {
  }
  return std::filesystem::path("config");
}

Params load_node_params(const std::filesystem::path& config_dir, int stage,
                        const std::string& team, const std::string& team_name) {
  Params params = load_params((config_dir / "params.yaml").string());
  params.stage = stage;
  params.team = team;
  params.team_name = team_name;
  return params;
}

rclcpp::QoS sensor_qos() {
  rclcpp::QoS qos(rclcpp::KeepLast(1));
  qos.reliable().durability_volatile();
  return qos;
}

rclcpp::QoS latched_qos() {
  rclcpp::QoS qos(rclcpp::KeepLast(1));
  qos.reliable().transient_local();
  return qos;
}

rclcpp::QoS event_qos() {
  rclcpp::QoS qos(rclcpp::KeepLast(128));
  qos.reliable().transient_local();
  return qos;
}

}  // namespace

RmtcBotNode::RmtcBotNode()
    : rclcpp::Node("rmtc_bot"),
      logger_("rmtc_bot.node"),
      team_(trim_upper(declare_parameter<std::string>("team", env_or("RMTC_TEAM", "A")))),
      team_name_(declare_parameter<std::string>("team_name",
                                                env_or("RMTC_TEAM_NAME", "RMTC-Bot"))),
      stage_(sanitize_stage(declare_parameter<int>("stage", 2))),
      config_dir_(resolve_config_dir(declare_parameter<std::string>("config_dir", ""))),
      params_(load_node_params(config_dir_, stage_, team_, team_name_)),
      quota_(params_.ai.max_calls_per_match, params_.ai.max_inflight,
             params_.ai.min_interval_ticks, params_.ai.periodic_interval_ticks),
      tracker_(params_.planning.resource_memory_ticks),
      event_input_(event_memory_),
      guard_(std::make_unique<SafetyGuard>(stage_)),
      sensor_qos_(sensor_qos()),
      latched_qos_(latched_qos()),
      event_qos_(event_qos()) {
  if (!std::filesystem::exists(config_dir_)) {
    logger_.error("config_dir 不存在: " + config_dir_.string());
  }

  token_ = env_or("RMTC_TEAM_TOKEN", "");
  Logger::register_secret(token_);
  const std::string base_url = env_or("RMTC_AI_BASE_URL", params_.ai.base_url);
  if (params_.ai.enabled && !token_.empty()) {
    llm_ = std::make_unique<LLMClient>(base_url, token_, params_.ai.model,
                                       params_.ai.max_tokens, params_.ai.temperature,
                                       params_.ai.request_timeout_s);
  } else {
    logger_.warn("AI 已禁用（缺少 RMTC_TEAM_TOKEN 或配置关闭）");
  }

  action_publisher_ = create_publisher<std_msgs::msg::String>(
      "/team_" + team_ + "/action", sensor_qos_);
  sender_ = std::make_unique<ActionSender>(
      [this](const std::string& text) {
        std_msgs::msg::String message;
        message.data = text;
        action_publisher_->publish(message);
      },
      &logger_);

  perception_subscription_ = create_subscription<std_msgs::msg::String>(
      "/team_" + team_ + "/perception", sensor_qos_,
      std::bind(&RmtcBotNode::on_perception, this, std::placeholders::_1));
  mission_subscription_ = create_subscription<std_msgs::msg::String>(
      "/referee/mission", latched_qos_,
      std::bind(&RmtcBotNode::on_mission, this, std::placeholders::_1));
  event_subscription_ = create_subscription<std_msgs::msg::String>(
      "/referee/event_text", event_qos_,
      std::bind(&RmtcBotNode::on_event, this, std::placeholders::_1));

  registry_ = std::make_unique<TeamRegistry>(this, team_, team_name_, latched_qos_, &logger_);
  registry_->announce();

  decision_config_path_ = config_dir_ / "decision_config.json";
  decision_enum_path_ = config_dir_ / "decision_enum.json";
  reload_decision_type(true);

  logger_.info(cat("节点启动 team=", team_, " stage=", stage_, " decision_type=",
                   decision_type_, " config_dir=", config_dir_.string(),
                   " ai=", llm_ ? "on" : "off"));
}

RmtcBotNode::~RmtcBotNode() {
  shutting_down_.store(true);
  for (auto& worker : workers_) {
    if (worker.thread.joinable()) {
      worker.thread.join();
    }
  }
  workers_.clear();
}

void RmtcBotNode::reload_decision_type(bool force) {
  std::error_code error;
  if (decision_config_path_.empty() ||
      !std::filesystem::exists(decision_config_path_, error)) {
    if (force) {
      logger_.error("decision_config.json 不存在: " + decision_config_path_.string());
    }
    return;
  }
  const auto mtime = std::filesystem::last_write_time(decision_config_path_, error);
  if (error) {
    return;
  }
  if (!force && decision_mtime_.has_value() && *decision_mtime_ == mtime) {
    return;
  }
  decision_mtime_ = mtime;

  std::string decision_type;
  std::string load_error;
  if (!load_decision_config(decision_config_path_.string(), decision_enum_path_.string(),
                            decision_type, load_error)) {
    logger_.error("decision_config.json 校验失败，保持 " + decision_type_ + ": " +
                  load_error);
    return;
  }
  if (decision_type != decision_type_) {
    logger_.info("decision_type 切换: " + decision_type_ + " -> " + decision_type);
    decision_type_ = decision_type;
  }
}

void RmtcBotNode::on_mission(const std_msgs::msg::String::SharedPtr msg) {
  missions_.update_from_text(msg->data);
  logger_.info("收到任务公告: " + Logger::redact(msg->data.substr(0, 200)));
}

void RmtcBotNode::on_event(const std_msgs::msg::String::SharedPtr msg) {
  const auto added = event_input_.from_ros_text(msg->data);
  if (!added.empty()) {
    logger_.info(cat("收到 ", added.size(), " 条新事件公告"));
  }
}

void RmtcBotNode::reap_workers() {
  for (auto it = workers_.begin(); it != workers_.end();) {
    if (it->done->load()) {
      if (it->thread.joinable()) {
        it->thread.join();
      }
      it = workers_.erase(it);
    } else {
      ++it;
    }
  }
}

void RmtcBotNode::on_perception(const std_msgs::msg::String::SharedPtr msg) {
  reap_workers();

  const ParseResult parsed = parse_perception(msg->data);
  if (!parsed.ok) {
    logger_.warn("感知解析失败，等待下一帧: " + parsed.error);
    return;
  }
  const Perception& perception = parsed.perception;

  if (perception.session_id != state_.session_id) {
    start_session(perception);
  }

  if (perception.tick < state_.tick) {
    logger_.debug(cat("丢弃旧 Tick 感知: ", perception.tick, " < ", state_.tick));
    return;
  }
  if (perception.tick == state_.tick && state_.has_seen_tick(perception.tick)) {
    // 暂停恢复后可能重发同一 Tick：只重发缓存动作，不重复推进规划器
    std::string resend_error;
    const bool resent =
        sender_->resend_cached(perception.tick, perception.session_id, resend_error);
    logger_.info(cat("同 Tick 重发 tick=", perception.tick,
                     "，重发缓存动作=", resent ? "true" : "false"));
    return;
  }

  state_.update(perception);
  tracker_.update(perception);
  grid_.update_from_perception(perception);
  event_input_.from_perception(perception);
  missions_.update_from_perception(perception);
  handle_last_action(perception);
  drain_ai_results(perception);
  reload_decision_type();
  maybe_request_ai(perception);

  DecisionContext ctx(perception.tick, stage_, perception, state_, grid_, tracker_, events_,
                      missions_, params_.planning);

  Intent intent;
  try {
    intent = router_.decide(ctx, decision_type_);
  } catch (const std::exception& exc) {
    logger_.error(cat("策略异常，降级 WAIT: ", exc.what()));
    intent = Intent("wait", std::nullopt, "decision_exception");
  }

  try {
    intent = guard_->check(intent, ctx);
  } catch (const std::exception& exc) {
    logger_.error(cat("安全预检异常，降级 WAIT: ", exc.what()));
    intent = Intent("wait", std::nullopt, "guard_exception");
  }

  // 幻术期间裁判会反转 MOVE 方向：安全预检按目标格通过后，再反转指令方向
  if (intent.action == "move" && intent.direction.has_value()) {
    const std::string opposite = opposite_direction(*intent.direction);
    if (opposite != *intent.direction) {
      last_move_attempt_ =
          std::make_pair(perception.tick, move_cell(perception.self.position,
                                                    *intent.direction));
      if (events_.illusion_active(perception.tick)) {
        intent = Intent("move", opposite, intent.reason + "|illusion_flip");
      }
    } else {
      last_move_attempt_.reset();
    }
  } else {
    last_move_attempt_.reset();
  }

  Action action;
  std::string action_error;
  if (!build_action(perception.tick, perception.session_id, intent.action, intent.direction,
                    action, action_error)) {
    logger_.error("动作构造失败，改为 WAIT: " + action_error);
    build_action(perception.tick, perception.session_id, "wait", std::nullopt, action,
                 action_error);
  }

  std::string send_error;
  const bool sent = sender_->send(action, send_error);
  if (!send_error.empty()) {
    logger_.error("动作发送前校验失败: " + send_error);
  }

  logger_.info(cat("tick=", perception.tick, " action=", action.action,
                   " dir=", action.direction.value_or("-"), " reason=", intent.reason,
                   " pos=", cell_to_string(perception.self.position),
                   " hp=", static_cast<int>(perception.self.hp),
                   " alive=", perception.self.alive ? "true" : "false",
                   " type=", decision_type_, " sent=", sent ? "true" : "false"));
}

void RmtcBotNode::start_session(const Perception& perception) {
  state_.begin_session(perception.session_id);
  tracker_.reset();
  grid_.reset();
  events_.reset();
  event_memory_.reset();
  missions_.reset();
  sender_->reset();
  last_move_attempt_.reset();
  quota_.reset_session(perception.session_id);
  logger_.info(cat("新场次 session=", perception.session_id, " tick=", perception.tick,
                   " map=", perception.map_width, "x", perception.map_height));
}

void RmtcBotNode::handle_last_action(const Perception& perception) {
  const auto ack = interpret_last_action(perception.self.last_action);
  if (!ack.has_value()) {
    return;
  }
  if (ack->error.has_value()) {
    logger_.warn("动作回执 " + ack->describe());
  } else {
    logger_.debug("动作回执 " + ack->describe());
  }

  // 撞墙/越界/争格失败后，短期回避该格，避免反复无效移动
  if (!ack->moved && ack->error.has_value() &&
      (*ack->error == "E_BLOCKED" || *ack->error == "E_OOB") &&
      last_move_attempt_.has_value() && last_move_attempt_->first == ack->tick) {
    grid_.mark_temp_blocked(last_move_attempt_->second, perception.tick, 5);
    logger_.info(cat("移动失败回避格 ", cell_to_string(last_move_attempt_->second),
                     "（tick=", ack->tick, " error=", *ack->error, "）"));
  }
}

void RmtcBotNode::maybe_request_ai(const Perception& perception) {
  if (stage_ == 1) {
    return;
  }
  if (!llm_) {
    if (!ai_disabled_logged_) {
      logger_.warn("AI 不可用，第二阶段将仅使用本地策略");
      ai_disabled_logged_ = true;
    }
    return;
  }
  const auto pending = event_memory_.pending_snapshot();
  if (!quota_.should_call(perception.tick, !pending.empty())) {
    return;
  }
  const int max_event_chars = params_.ai.max_event_chars;
  const Json::Value messages =
      build_messages(perception, pending, event_memory_.recent_text(max_event_chars),
                     max_event_chars);
  if (!quota_.begin_call(perception.tick)) {
    return;
  }

  const std::string session_id = perception.session_id;
  const int tick = perception.tick;
  auto done = std::make_shared<std::atomic<bool>>(false);
  std::thread worker([this, session_id, tick, messages, pending, done]() {
    ai_worker(session_id, tick, messages, pending);
    done->store(true);
  });
  workers_.push_back(AiWorker{std::move(worker), done});
}

void RmtcBotNode::ai_worker(const std::string& session_id, int tick,
                            const Json::Value& messages,
                            const std::vector<std::string>& pending) {
  LLMResult result;
  try {
    result = llm_->chat(messages);
  } catch (const std::exception& exc) {
    result.ok = false;
    result.error_code = "client_error";
    logger_.warn(cat("AI 调用异常 tick=", tick, " what=", exc.what()));
  }
  quota_.end_call(result.ok, result.error_code.value_or(""), tick);
  if (result.ok && result.content.has_value()) {
    std::lock_guard<std::mutex> lock(results_mutex_);
    AiResult item;
    item.session_id = session_id;
    item.tick = tick;
    item.content = *result.content;
    item.pending = pending;
    ai_results_.push_back(std::move(item));
  } else {
    logger_.warn(cat("AI 调用失败 tick=", tick, " code=", result.error_code.value_or(""),
                     "，本地策略继续"));
  }
}

void RmtcBotNode::drain_ai_results(const Perception& perception) {
  std::deque<AiResult> results;
  {
    std::lock_guard<std::mutex> lock(results_mutex_);
    results.swap(ai_results_);
  }
  for (const auto& result : results) {
    if (!is_fresh(result.session_id, result.tick, perception.session_id, perception.tick)) {
      logger_.debug(cat("丢弃过期/异场 AI 结果 session=", result.session_id,
                        " tick=", result.tick));
      continue;
    }
    LLMDecision decision;
    std::string error;
    if (!validate_llm_output(result.content, perception.session_id, result.tick,
                             perception.map_width, perception.map_height, decision,
                             error)) {
      logger_.warn("AI 输出校验失败，降级本地策略: " + error);
      event_memory_.ack_pending(result.pending);
      continue;
    }
    events_.apply(decision, perception.tick, &grid_);
    event_memory_.ack_pending(result.pending);
    logger_.info(cat("AI 结果已应用 strategy=", decision.strategy,
                     " events=", decision.events.size(),
                     " summary=", Logger::redact(decision.summary.substr(0, 120))));
  }
}

void RmtcBotNode::shutdown_log() {
  const QuotaSnapshot snapshot = quota_.snapshot();
  logger_.info(cat("本场 AI 本地记录: calls=", snapshot.calls_started,
                   " inflight=", snapshot.inflight,
                   " qualified=", snapshot.qualified ? "true" : "false",
                   " exhausted=", snapshot.exhausted ? "true" : "false",
                   " last_error=", snapshot.last_error));
}

}  // namespace rmtc_bot
