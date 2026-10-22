#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rmtc_bot/action/action_schema.hpp"
#include "rmtc_bot/action/action_sender.hpp"

namespace {

rmtc_bot::Action make_action(const std::string& name,
                             const std::optional<std::string>& direction) {
  rmtc_bot::Action action;
  action.tick = 7;
  action.session_id = "s1";
  action.action = name;
  action.direction = direction;
  return action;
}

}  // namespace

TEST(ActionSchema, MoveIsValid) {
  const auto action = make_action("move", "right");
  std::string error;
  EXPECT_TRUE(rmtc_bot::validate_action(action, error)) << error;
}

TEST(ActionSchema, AttackIsValid) {
  const auto action = make_action("attack", "up");
  std::string error;
  EXPECT_TRUE(rmtc_bot::validate_action(action, error)) << error;
}

TEST(ActionSchema, DefendIsValid) {
  const auto action = make_action("defend", std::nullopt);
  std::string error;
  EXPECT_TRUE(rmtc_bot::validate_action(action, error)) << error;
}

TEST(ActionSchema, WaitIsValid) {
  const auto action = make_action("wait", std::nullopt);
  std::string error;
  EXPECT_TRUE(rmtc_bot::validate_action(action, error)) << error;
}

TEST(ActionSchema, MoveWithoutDirectionRejected) {
  const auto action = make_action("move", std::nullopt);
  std::string error;
  EXPECT_FALSE(rmtc_bot::validate_action(action, error));
  EXPECT_FALSE(error.empty());
}

TEST(ActionSchema, WaitWithDirectionRejected) {
  const auto action = make_action("wait", "left");
  std::string error;
  EXPECT_FALSE(rmtc_bot::validate_action(action, error));
}

TEST(ActionSchema, UppercaseActionRejected) {
  const auto action = make_action("MOVE", "left");
  std::string error;
  EXPECT_FALSE(rmtc_bot::validate_action(action, error));
}

TEST(ActionSchema, UppercaseDirectionRejected) {
  const auto action = make_action("move", "LEFT");
  std::string error;
  EXPECT_FALSE(rmtc_bot::validate_action(action, error));
}

TEST(ActionSchema, InvalidDirectionRejected) {
  const auto action = make_action("attack", "north");
  std::string error;
  EXPECT_FALSE(rmtc_bot::validate_action(action, error));
}

TEST(ActionSchema, EmptySessionRejected) {
  auto action = make_action("wait", std::nullopt);
  action.session_id.clear();
  std::string error;
  EXPECT_FALSE(rmtc_bot::validate_action(action, error));
}

TEST(ActionSchema, SerializeMatchesSchema) {
  const auto action = make_action("move", "right");
  std::string text;
  std::string error;
  ASSERT_TRUE(rmtc_bot::serialize_action(action, text, error)) << error;
  rmtc_bot::Action parsed;
  ASSERT_TRUE(rmtc_bot::parse_action_json(text, parsed, error)) << error;
  EXPECT_EQ(parsed.tick, 7);
  EXPECT_EQ(parsed.session_id, "s1");
  EXPECT_EQ(parsed.action, "move");
  ASSERT_TRUE(parsed.direction.has_value());
  EXPECT_EQ(*parsed.direction, "right");
  EXPECT_LE(text.size(), 4096u);
}

TEST(ActionSchema, ParseRejectsUnknownField) {
  rmtc_bot::Action parsed;
  std::string error;
  EXPECT_FALSE(rmtc_bot::parse_action_json(
      R"({"tick":1,"session_id":"s","action":"wait","extra":1})", parsed, error));
}

TEST(ActionSchema, ParseRejectsStringTick) {
  rmtc_bot::Action parsed;
  std::string error;
  EXPECT_FALSE(rmtc_bot::parse_action_json(
      R"({"tick":"1","session_id":"s","action":"wait"})", parsed, error));
}

TEST(ActionSchema, ParseRejectsMissingFields) {
  rmtc_bot::Action parsed;
  std::string error;
  EXPECT_FALSE(rmtc_bot::parse_action_json(
      R"({"session_id":"s","action":"wait"})", parsed, error));
}

TEST(ActionSender, SendsOncePerTick) {
  std::vector<std::string> published;
  rmtc_bot::ActionSender sender(
      [&published](const std::string& text) { published.push_back(text); });
  std::string error;
  EXPECT_TRUE(sender.send(make_action("wait", std::nullopt), error)) << error;
  EXPECT_FALSE(sender.send(make_action("wait", std::nullopt), error));
  EXPECT_EQ(published.size(), 1u);
  EXPECT_EQ(sender.last_sent_tick(), 7);
}

TEST(ActionSender, ResendCachedOnlyForSameTickAndSession) {
  std::vector<std::string> published;
  rmtc_bot::ActionSender sender(
      [&published](const std::string& text) { published.push_back(text); });
  std::string error;
  ASSERT_TRUE(sender.send(make_action("wait", std::nullopt), error));
  EXPECT_TRUE(sender.resend_cached(7, "s1", error));
  EXPECT_FALSE(sender.resend_cached(8, "s1", error));
  EXPECT_FALSE(sender.resend_cached(7, "s2", error));
  EXPECT_EQ(published.size(), 2u);
}

TEST(ActionSender, ResetClearsCache) {
  std::vector<std::string> published;
  rmtc_bot::ActionSender sender(
      [&published](const std::string& text) { published.push_back(text); });
  std::string error;
  ASSERT_TRUE(sender.send(make_action("wait", std::nullopt), error));
  sender.reset();
  EXPECT_FALSE(sender.cached().has_value());
  EXPECT_EQ(sender.last_sent_tick(), -1);
  EXPECT_FALSE(sender.resend_cached(7, "s1", error));
}

TEST(ActionSender, InvalidActionIsNotPublished) {
  std::vector<std::string> published;
  rmtc_bot::ActionSender sender(
      [&published](const std::string& text) { published.push_back(text); });
  std::string error;
  EXPECT_FALSE(sender.send(make_action("move", std::nullopt), error));
  EXPECT_FALSE(error.empty());
  EXPECT_TRUE(published.empty());
}
