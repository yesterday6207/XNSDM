#include <curl/curl.h>
#include <rclcpp/rclcpp.hpp>

#include <exception>
#include <memory>

#include "rmtc_bot/main_node.hpp"
#include "rmtc_bot/utils/logger.hpp"

int main(int argc, char** argv) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  rclcpp::init(argc, argv);

  int exit_code = 0;
  try {
    auto node = std::make_shared<rmtc_bot::RmtcBotNode>();
    rclcpp::spin(node);
    node->shutdown_log();
  } catch (const std::exception& exc) {
    rmtc_bot::Logger("rmtc_bot.node").error(std::string("节点异常退出: ") + exc.what());
    exit_code = 1;
  }

  rclcpp::shutdown();
  curl_global_cleanup();
  return exit_code;
}
