#pragma once

#include <json/json.h>

#include <optional>
#include <string>

namespace rmtc_bot {

struct LLMResult {
  bool ok = false;
  std::optional<std::string> content;
  std::optional<std::string> error_code;
  std::optional<long> http_status;
};

/// OpenAI 兼容网关客户端；同步调用，由主节点放到独立线程中使用。
class LLMClient {
 public:
  LLMClient(std::string base_url, std::string token, std::string model = "",
            int max_tokens = 512, double temperature = 0.2, double timeout_s = 4.0);

  std::string completions_url() const;
  std::string health_url() const;

  bool healthz() const;
  LLMResult chat(const Json::Value& messages) const;

  const std::string& base_url() const;
  bool has_token() const;

 private:
  std::string base_url_;
  std::string token_;
  std::string model_;
  int max_tokens_;
  double temperature_;
  double timeout_s_;
};

}  // namespace rmtc_bot
