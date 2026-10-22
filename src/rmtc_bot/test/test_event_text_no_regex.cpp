#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rmtc_bot/ai/event_memory.hpp"
#include "rmtc_bot/events/event_input.hpp"
#include "rmtc_bot/events/mission_input.hpp"
#include "rmtc_bot/types.hpp"

namespace {

using rmtc_bot::Cell;

std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

rmtc_bot::Perception perception_with_events(const std::string& text) {
  rmtc_bot::Perception perception;
  perception.tick = 7;
  perception.session_id = "session-1";
  perception.event_text = text;
  return perception;
}

}  // namespace

TEST(EventMemory, DeduplicatesIdenticalLines) {
  rmtc_bot::EventMemory memory;
  const auto added = memory.ingest_text("幻术启动\n\n幻术启动\n  \n天使降临");
  ASSERT_EQ(added.size(), 2u);
  EXPECT_EQ(added[0], "幻术启动");
  EXPECT_EQ(added[1], "天使降临");
  const auto stats = memory.stats();
  EXPECT_EQ(stats.seen, 2u);
  EXPECT_EQ(stats.pending, 2u);
}

TEST(EventMemory, EmptyTextAddsNothing) {
  rmtc_bot::EventMemory memory;
  EXPECT_TRUE(memory.ingest_text("").empty());
  EXPECT_TRUE(memory.ingest_text("   \n\t\n").empty());
  EXPECT_EQ(memory.stats().seen, 0u);
}

TEST(EventMemory, PendingAckRemovesOnlyGivenItems) {
  rmtc_bot::EventMemory memory;
  memory.ingest_text("A\nB\nC");
  memory.ack_pending({"B", "missing"});
  const auto pending = memory.pending_snapshot();
  ASSERT_EQ(pending.size(), 2u);
  EXPECT_EQ(pending[0], "A");
  EXPECT_EQ(pending[1], "C");
  EXPECT_EQ(memory.stats().seen, 3u);
}

TEST(EventMemory, RecentTextKeepsLatestWithinBudget) {
  rmtc_bot::EventMemory memory;
  memory.ingest_text("aaa\nbbb\nccc");
  EXPECT_EQ(memory.recent_text(7), "ccc");
  EXPECT_EQ(memory.recent_text(200), "aaa\nbbb\nccc");
  EXPECT_EQ(memory.recent_text(0), "");
}

TEST(EventMemory, ResetClearsEverything) {
  rmtc_bot::EventMemory memory;
  memory.ingest_text("事件一\n事件二");
  memory.reset();
  const auto stats = memory.stats();
  EXPECT_EQ(stats.seen, 0u);
  EXPECT_EQ(stats.pending, 0u);
  EXPECT_TRUE(memory.recent_text().empty());
  EXPECT_TRUE(memory.ingest_text("事件一").size() == 1u);
}

TEST(EventMemory, MaxItemsDropsOldestAndPending) {
  rmtc_bot::EventMemory memory(2);
  memory.ingest_text("a\nb\nc");
  const auto stats = memory.stats();
  EXPECT_EQ(stats.seen, 2u);
  const auto pending = memory.pending_snapshot();
  ASSERT_EQ(pending.size(), 2u);
  EXPECT_EQ(pending[0], "b");
  EXPECT_EQ(pending[1], "c");
  EXPECT_EQ(memory.recent_text(200), "b\nc");
}

TEST(EventInput, ExtractAnnouncementsSplitsTrimmedLines) {
  const auto lines = rmtc_bot::extract_announcements("  第一行 \n\n 第二行\n无换行结尾");
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "第一行");
  EXPECT_EQ(lines[1], "第二行");
  EXPECT_EQ(lines[2], "无换行结尾");
  EXPECT_TRUE(rmtc_bot::extract_announcements("").empty());
}

TEST(EventInput, RosTextGoesThroughDeduplication) {
  rmtc_bot::EventMemory memory;
  rmtc_bot::EventInput input(memory);
  EXPECT_EQ(input.from_ros_text("爆炸预警\n爆炸预警").size(), 1u);
  EXPECT_EQ(input.from_ros_text("爆炸预警").size(), 0u);
  EXPECT_EQ(memory.stats().seen, 1u);
}

TEST(EventInput, PerceptionTextIsCachedWithoutSemanticParsing) {
  rmtc_bot::EventMemory memory;
  rmtc_bot::EventInput input(memory);
  const auto added = input.from_perception(perception_with_events("world_split 第 40 tick\n通行费 5"));
  ASSERT_EQ(added.size(), 2u);
  EXPECT_EQ(added[0], "world_split 第 40 tick");
  EXPECT_EQ(added[1], "通行费 5");
  EXPECT_EQ(memory.stats().pending, 2u);
}

TEST(MissionInput, DefaultStandardMissionsWhenEmpty) {
  rmtc_bot::MissionInput missions;
  const auto types = missions.incomplete_types();
  ASSERT_EQ(types.size(), 3u);
  EXPECT_TRUE(missions.needs("normal_resources"));
  EXPECT_TRUE(missions.needs("high_resources"));
  EXPECT_TRUE(missions.needs("reach"));
  EXPECT_FALSE(missions.needs("duel"));
}

TEST(MissionInput, CompletedMissionIsFilteredOut) {
  rmtc_bot::MissionInput missions;
  rmtc_bot::Perception perception;
  rmtc_bot::Mission normal;
  normal.id = "m1";
  normal.type = "normal_resources";
  rmtc_bot::Mission reach;
  reach.id = "m2";
  reach.type = "reach";
  reach.position = Cell{5, 6};
  perception.missions = {normal, reach};
  perception.score.mission_completed_ids = {"m1"};
  missions.update_from_perception(perception);
  const auto types = missions.incomplete_types();
  ASSERT_EQ(types.size(), 1u);
  EXPECT_TRUE(missions.needs("reach"));
  EXPECT_FALSE(missions.needs("normal_resources"));
  EXPECT_EQ(*missions.reach_target(Cell{12, 12}), (Cell{5, 6}));
}

TEST(MissionInput, ReachTargetFallsBackOrDisappears) {
  // 感知未给出任务列表时仍按公开三项任务执行，使用默认到达点
  rmtc_bot::MissionInput empty;
  ASSERT_TRUE(empty.reach_target(Cell{12, 12}).has_value());
  EXPECT_EQ(*empty.reach_target(Cell{12, 12}), (Cell(12, 12)));
  EXPECT_TRUE(empty.needs("reach"));

  rmtc_bot::MissionInput without_position;
  rmtc_bot::Perception perception;
  rmtc_bot::Mission reach;
  reach.id = "m1";
  reach.type = "reach";
  perception.missions = {reach};
  without_position.update_from_perception(perception);
  ASSERT_TRUE(without_position.reach_target(Cell{12, 12}).has_value());
  EXPECT_EQ(*without_position.reach_target(Cell{12, 12}), (Cell(12, 12)));

  rmtc_bot::MissionInput completed;
  perception.score.mission_completed_ids = {"m1"};
  completed.update_from_perception(perception);
  EXPECT_FALSE(completed.reach_target(Cell{12, 12}).has_value());
}

TEST(EventSource, NoRegexSemanticParsingInSources) {
  const std::filesystem::path root = std::filesystem::path(RMTC_SOURCE_DIR);
  const std::vector<std::string> forbidden{"<regex>", "std::regex",
                                           "regex_search", "regex_match", "boost::regex"};
  int scanned = 0;
  for (const auto& dir : {root / "src", root / "include"}) {
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const std::string text = read_file(entry.path());
      if (text.empty()) {
        continue;
      }
      ++scanned;
      for (const auto& needle : forbidden) {
        EXPECT_EQ(text.find(needle), std::string::npos)
            << entry.path().string() << " 含正则语义解析: " << needle;
      }
    }
  }
  EXPECT_GT(scanned, 30);
}
