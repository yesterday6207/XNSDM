#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rmtc_bot/ai/output_validator.hpp"
#include "rmtc_bot/ai/quota_manager.hpp"
#include "rmtc_bot/decision/local_fallback.hpp"
#include "rmtc_bot/events/event_state.hpp"
#include "rmtc_bot/events/mission_input.hpp"
#include "rmtc_bot/perception/entity_tracker.hpp"
#include "rmtc_bot/perception/state_store.hpp"
#include "rmtc_bot/planning/grid_map.hpp"
#include "rmtc_bot/utils/config_loader.hpp"

namespace {

using rmtc_bot::Cell;

const char* kValidJson =
    R"({"strategy":"collect","summary":"前往资源点","events":[
         {"type":"angel","active_from":10,"active_until":20,
          "area":[24,24,-3,-5],"point":[3,4],"recommendation":"heal"}]})";

rmtc_bot::LLMDecision validate(const std::string& content, int tick, bool* ok_out = nullptr) {
  rmtc_bot::LLMDecision decision;
  std::string error;
  const bool ok =
      rmtc_bot::validate_llm_output(content, "session-1", tick, 10, 8, decision, error);
  if (ok_out != nullptr) {
    *ok_out = ok;
  }
  return decision;
}

struct FallbackFixture {
  rmtc_bot::Perception perception;
  rmtc_bot::StateStore state;
  rmtc_bot::GridMap grid;
  rmtc_bot::EntityTracker tracker;
  rmtc_bot::EventState events;
  rmtc_bot::MissionInput missions;
  rmtc_bot::PlanningParams planning;

  explicit FallbackFixture(bool alive = true) {
    perception.tick = 10;
    perception.session_id = "session-1";
    perception.map_width = 25;
    perception.map_height = 25;
    perception.vision_radius = 4;
    perception.self.team = "A";
    perception.self.robot_id = "A1";
    perception.self.position = Cell{12, 12};
    perception.self.hp = 100.0;
    perception.self.alive = alive;
    state.update(perception);
    grid.set_map(25, 25);
    tracker.update(perception);
    missions.update_from_perception(perception);
  }

  rmtc_bot::DecisionContext context(int stage = 2, int tick = 10) {
    return rmtc_bot::DecisionContext(tick, stage, perception, state, grid, tracker, events,
                                     missions, planning);
  }
};

}  // namespace

TEST(LlmOutput, AcceptsValidHighLevelDecision) {
  bool ok = false;
  const auto decision = validate(kValidJson, 5, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(decision.strategy, "collect");
  EXPECT_EQ(decision.summary, "前往资源点");
  EXPECT_EQ(decision.source_tick, 5);
  EXPECT_EQ(decision.source_session, "session-1");
  ASSERT_EQ(decision.events.size(), 1u);
  EXPECT_EQ(decision.events[0].type, "angel");
  EXPECT_EQ(decision.events[0].recommendation, "heal");
  ASSERT_TRUE(decision.events[0].area.has_value());
  EXPECT_EQ(decision.events[0].area->x1, 0);
  EXPECT_EQ(decision.events[0].area->y1, 0);
  EXPECT_EQ(decision.events[0].area->x2, 9);
  EXPECT_EQ(decision.events[0].area->y2, 7);
  ASSERT_TRUE(decision.events[0].point.has_value());
  EXPECT_EQ(*decision.events[0].point, (Cell(3, 4)));
}

TEST(LlmOutput, StripsCodeFence) {
  const std::string content = "```json\n" + std::string(kValidJson) + "\n```";
  bool ok = false;
  const auto decision = validate(content, 5, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(decision.strategy, "collect");
}

TEST(LlmOutput, RejectsEmptyContent) {
  bool ok = true;
  validate("   \n\t", 5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, RejectsNonObjectTopLevel) {
  bool ok = true;
  validate(R"(["collect"])", 5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, RejectsForbiddenActionField) {
  bool ok = true;
  validate(R"({"strategy":"collect","action":"move","events":[]})", 5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, RejectsForbiddenNestedField) {
  bool ok = true;
  validate(
      R"({"strategy":"collect","events":[{"type":"angel","direction":"up"}]})", 5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, RejectsInvalidStrategyAndEventType) {
  bool ok = true;
  validate(R"({"strategy":"rush","events":[]})", 5, &ok);
  EXPECT_FALSE(ok);
  validate(R"({"strategy":"collect","events":[{"type":"meteor"}]})", 5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, RejectsMalformedAreaAndTime) {
  bool ok = true;
  validate(R"({"strategy":"collect","events":[{"type":"angel","area":[1,2,3]}]})", 5, &ok);
  EXPECT_FALSE(ok);
  validate(
      R"({"strategy":"collect","events":[{"type":"angel","active_from":-1}]})", 5, &ok);
  EXPECT_FALSE(ok);
  validate(
      R"({"strategy":"collect","events":[{"type":"angel","active_from":20,
                                          "active_until":10}]})",
      5, &ok);
  EXPECT_FALSE(ok);
  validate(R"({"strategy":"collect","events":[{"type":"angel","recommendation":"jump"}]})",
           5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, DropsPointOutsideMap) {
  bool ok = false;
  const auto decision = validate(
      R"({"strategy":"explore","events":[{"type":"angel","point":[99,99]}]})", 5, &ok);
  ASSERT_TRUE(ok);
  ASSERT_EQ(decision.events.size(), 1u);
  EXPECT_FALSE(decision.events[0].point.has_value());
}

TEST(LlmOutput, DropsExpiredEvent) {
  bool ok = false;
  const auto decision =
      validate(R"({"strategy":"avoid","events":[{"type":"explosion","active_until":10}]})",
               10, &ok);
  ASSERT_TRUE(ok);
  EXPECT_TRUE(decision.events.empty());
}

TEST(LlmOutput, RejectsTooManyEventsAndBadSummary) {
  bool ok = true;
  std::string events = "[";
  for (int i = 0; i < 17; ++i) {
    if (i > 0) {
      events += ",";
    }
    events += R"({"type":"other"})";
  }
  events += "]";
  validate("{\"strategy\":\"explore\",\"events\":" + events + "}", 5, &ok);
  EXPECT_FALSE(ok);
  validate(R"({"strategy":"explore","summary":123,"events":[]})", 5, &ok);
  EXPECT_FALSE(ok);
  validate(R"({"strategy":"explore","events":{}})", 5, &ok);
  EXPECT_FALSE(ok);
}

TEST(LlmOutput, TruncatesLongSummary) {
  const std::string long_summary(600, 'x');
  bool ok = false;
  const auto decision = validate(
      "{\"strategy\":\"explore\",\"summary\":\"" + long_summary + "\",\"events\":[]}", 5, &ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(decision.summary.size(), 500u);
}

TEST(Quota, ExhaustedBlocksFurtherCalls) {
  rmtc_bot::QuotaManager quota(40, 2, 8, 80);
  ASSERT_TRUE(quota.begin_call(1));
  quota.end_call(false, "quota_exceeded", 1);
  const auto snapshot = quota.snapshot();
  EXPECT_TRUE(snapshot.exhausted);
  EXPECT_EQ(snapshot.last_error, "quota_exceeded");
  EXPECT_FALSE(quota.should_call(100, true));
  EXPECT_FALSE(quota.begin_call(100));
}

TEST(Quota, RateLimitBacksOff) {
  rmtc_bot::QuotaManager quota(40, 2, 8, 80);
  ASSERT_TRUE(quota.begin_call(10));
  quota.end_call(false, "rate_limit", 10);
  EXPECT_FALSE(quota.should_call(11, true));
  EXPECT_FALSE(quota.should_call(14, true));
  EXPECT_TRUE(quota.should_call(18, true));
}

TEST(Quota, MarksQualifiedAfterSuccess) {
  rmtc_bot::QuotaManager quota(40, 2, 8, 80);
  ASSERT_TRUE(quota.begin_call(3));
  quota.end_call(true, "", 3);
  const auto snapshot = quota.snapshot();
  EXPECT_TRUE(snapshot.qualified);
  EXPECT_EQ(snapshot.last_success_tick, 3);
  EXPECT_TRUE(snapshot.last_error.empty());
}

TEST(Quota, RespectsInflightAndCallLimit) {
  rmtc_bot::QuotaManager single(40, 1, 0, 80);
  ASSERT_TRUE(single.begin_call(1));
  EXPECT_FALSE(single.begin_call(2));
  single.end_call(true, "", 1);
  EXPECT_TRUE(single.begin_call(2));
  single.end_call(true, "", 2);
  EXPECT_EQ(single.snapshot().calls_started, 2);

  rmtc_bot::QuotaManager limited(2, 2, 0, 80);
  ASSERT_TRUE(limited.begin_call(1));
  limited.end_call(true, "", 1);
  ASSERT_TRUE(limited.begin_call(2));
  limited.end_call(true, "", 2);
  EXPECT_FALSE(limited.begin_call(3));
  EXPECT_FALSE(limited.should_call(10, true));
}

TEST(Quota, IntervalAndPeriodicGating) {
  rmtc_bot::QuotaManager quota(40, 2, 8, 80);
  EXPECT_TRUE(quota.should_call(0, false));
  ASSERT_TRUE(quota.begin_call(10));
  quota.end_call(true, "", 10);
  EXPECT_FALSE(quota.should_call(11, true));
  EXPECT_TRUE(quota.should_call(18, true));
  EXPECT_FALSE(quota.should_call(20, false));
  EXPECT_TRUE(quota.should_call(90, false));
}

TEST(Quota, ResetSessionClearsState) {
  rmtc_bot::QuotaManager quota(40, 2, 8, 80);
  ASSERT_TRUE(quota.begin_call(1));
  quota.end_call(false, "quota_exceeded", 1);
  ASSERT_TRUE(quota.snapshot().exhausted);
  quota.reset_session(std::optional<std::string>("session-2"));
  const auto snapshot = quota.snapshot();
  EXPECT_FALSE(snapshot.exhausted);
  EXPECT_FALSE(snapshot.qualified);
  EXPECT_EQ(snapshot.calls_started, 0);
  EXPECT_EQ(snapshot.inflight, 0);
  ASSERT_TRUE(snapshot.session_id.has_value());
  EXPECT_EQ(*snapshot.session_id, "session-2");
  EXPECT_TRUE(quota.should_call(0, false));
}

TEST(Quota, EndCallWithoutBeginClampsInflight) {
  rmtc_bot::QuotaManager quota(40, 2, 8, 80);
  quota.end_call(false, "timeout", 4);
  EXPECT_EQ(quota.snapshot().inflight, 0);
  EXPECT_EQ(quota.snapshot().last_error, "timeout");
}

TEST(LocalFallback, ReturnsValidIntentWhenAlive) {
  FallbackFixture fixture;
  auto ctx = fixture.context();
  const auto intent = rmtc_bot::local_fallback_decide(ctx, "timeout");
  EXPECT_TRUE(intent.action == "move" || intent.action == "wait" ||
              intent.action == "attack" || intent.action == "defend");
  if (intent.action == "move") {
    ASSERT_TRUE(intent.direction.has_value());
    EXPECT_TRUE(*intent.direction == "up" || *intent.direction == "down" ||
                *intent.direction == "left" || *intent.direction == "right");
  }
  EXPECT_NE(intent.reason.find("fallback:timeout"), std::string::npos);
}

TEST(LocalFallback, DeadRobotWaits) {
  FallbackFixture fixture(false);
  auto ctx = fixture.context();
  const auto intent = rmtc_bot::local_fallback_decide(ctx, "dead");
  EXPECT_EQ(intent.action, "wait");
  EXPECT_FALSE(intent.direction.has_value());
  EXPECT_NE(intent.reason.find("stage2_dead"), std::string::npos);
  EXPECT_NE(intent.reason.find("fallback:dead"), std::string::npos);
}

TEST(LocalFallback, EmptyReasonLeavesIntentUnchanged) {
  FallbackFixture fixture;
  auto ctx = fixture.context();
  const auto intent = rmtc_bot::local_fallback_decide(ctx);
  EXPECT_FALSE(intent.action.empty());
  EXPECT_EQ(intent.reason.find("fallback:"), std::string::npos);
}
