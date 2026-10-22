#include "rmtc_bot/ai/llm_client.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <memory>
#include <mutex>

namespace rmtc_bot {

namespace {

constexpr std::size_t kMaxPayloadBytes = 2097152;

std::size_t write_callback(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
  auto* buffer = static_cast<std::string*>(userdata);
  buffer->append(data, size * nmemb);
  return size * nmemb;
}

void ensure_curl_initialized() {
  static std::once_flag once;
  std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

std::string trim_slashes(const std::string& url) {
  std::string result = url;
  while (!result.empty() && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

std::optional<std::string> extract_error_code(const std::string& body) {
  Json::CharReaderBuilder builder;
  std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
  Json::Value data;
  std::string errors;
  if (!reader->parse(body.data(), body.data() + body.size(), &data, &errors)) {
    return std::nullopt;
  }
  if (!data.isObject()) {
    return std::nullopt;
  }
  const Json::Value& error = data["error"];
  if (error.isObject() && error["code"].isString()) {
    return error["code"].asString();
  }
  return std::nullopt;
}

struct CurlHandle {
  CURL* handle = nullptr;
  curl_slist* headers = nullptr;
  ~CurlHandle() {
    if (headers != nullptr) {
      curl_slist_free_all(headers);
    }
    if (handle != nullptr) {
      curl_easy_cleanup(handle);
    }
  }
};

}  // namespace

LLMClient::LLMClient(std::string base_url, std::string token, std::string model,
                     int max_tokens, double temperature, double timeout_s)
    : base_url_(trim_slashes(base_url)),
      token_(std::move(token)),
      model_(std::move(model)),
      max_tokens_(max_tokens),
      temperature_(temperature),
      timeout_s_(timeout_s) {}

std::string LLMClient::completions_url() const {
  const std::string suffix = "/v1/chat/completions";
  if (base_url_.size() >= suffix.size() &&
      base_url_.compare(base_url_.size() - suffix.size(), suffix.size(), suffix) == 0) {
    return base_url_;
  }
  if (base_url_.size() >= 3 && base_url_.compare(base_url_.size() - 3, 3, "/v1") == 0) {
    return base_url_ + "/chat/completions";
  }
  return base_url_ + suffix;
}

std::string LLMClient::health_url() const {
  const std::string suffix = "/v1/chat/completions";
  std::string base = base_url_;
  if (base.size() >= suffix.size() &&
      base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
    base = base.substr(0, base.size() - suffix.size());
  } else if (base.size() >= 3 && base.compare(base.size() - 3, 3, "/v1") == 0) {
    base = base.substr(0, base.size() - 3);
  }
  return base + "/healthz";
}

bool LLMClient::healthz() const {
  ensure_curl_initialized();
  CurlHandle curl;
  curl.handle = curl_easy_init();
  if (curl.handle == nullptr) {
    return false;
  }
  std::string body;
  const long timeout_ms =
      static_cast<long>(std::max(0.2, std::min(2.0, timeout_s_)) * 1000.0);
  curl_easy_setopt(curl.handle, CURLOPT_URL, health_url().c_str());
  curl_easy_setopt(curl.handle, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl.handle, CURLOPT_NOSIGNAL, 1L);
  const CURLcode code = curl_easy_perform(curl.handle);
  if (code != CURLE_OK) {
    return false;
  }
  long status = 0;
  curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
  return status >= 200 && status < 300;
}

LLMResult LLMClient::chat(const Json::Value& messages) const {
  LLMResult result;
  if (!messages.isArray() || messages.empty()) {
    result.error_code = "empty_messages";
    return result;
  }
  if (token_.empty()) {
    result.error_code = "missing_token";
    return result;
  }

  Json::Value payload(Json::objectValue);
  payload["messages"] = messages;
  payload["max_tokens"] = max_tokens_;
  payload["temperature"] = temperature_;
  payload["stream"] = false;
  if (!model_.empty()) {
    payload["model"] = model_;
  }

  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["emitUTF8"] = true;
  const std::string body = Json::writeString(builder, payload);
  if (body.size() > kMaxPayloadBytes) {
    result.error_code = "payload_too_large";
    return result;
  }

  ensure_curl_initialized();
  CurlHandle curl;
  curl.handle = curl_easy_init();
  if (curl.handle == nullptr) {
    result.error_code = "client_error";
    return result;
  }

  const std::string authorization = "Authorization: Bearer " + token_;
  curl.headers = curl_slist_append(curl.headers, "Content-Type: application/json");
  curl.headers = curl_slist_append(curl.headers, authorization.c_str());

  std::string response;
  const long timeout_ms = static_cast<long>(std::max(0.2, timeout_s_) * 1000.0);
  curl_easy_setopt(curl.handle, CURLOPT_URL, completions_url().c_str());
  curl_easy_setopt(curl.handle, CURLOPT_POST, 1L);
  curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl.handle, CURLOPT_POSTFIELDSIZE,
                   static_cast<long>(body.size()));
  curl_easy_setopt(curl.handle, CURLOPT_HTTPHEADER, curl.headers);
  curl_easy_setopt(curl.handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl.handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl.handle, CURLOPT_WRITEDATA, &response);

  const CURLcode code = curl_easy_perform(curl.handle);
  if (code == CURLE_OPERATION_TIMEDOUT) {
    result.error_code = "timeout";
    return result;
  }
  if (code != CURLE_OK) {
    result.error_code = "network_error";
    return result;
  }

  long status = 0;
  curl_easy_getinfo(curl.handle, CURLINFO_RESPONSE_CODE, &status);
  result.http_status = status;

  if (status < 200 || status >= 300) {
    const auto error_code = extract_error_code(response);
    result.error_code = error_code.value_or("http_" + std::to_string(status));
    return result;
  }

  Json::CharReaderBuilder reader_builder;
  std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
  Json::Value data;
  std::string errors;
  if (!reader->parse(response.data(), response.data() + response.size(), &data, &errors)) {
    result.error_code = "invalid_response";
    return result;
  }
  if (!data.isObject() || !data["choices"].isArray() || data["choices"].empty()) {
    result.error_code = "invalid_response";
    return result;
  }
  const Json::Value& content = data["choices"][0]["message"]["content"];
  if (!content.isString()) {
    result.error_code = "invalid_response";
    return result;
  }
  result.ok = true;
  result.content = content.asString();
  return result;
}

const std::string& LLMClient::base_url() const { return base_url_; }

bool LLMClient::has_token() const { return !token_.empty(); }

}  // namespace rmtc_bot
