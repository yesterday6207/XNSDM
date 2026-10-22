#include <gtest/gtest.h>

#include <string>

#include "rmtc_bot/perception/relative_map.hpp"

namespace {

std::string make_grid(int dx, int dy, bool include_center = true,
                      bool duplicate = false) {
  std::string text;
  for (int row = 0; row < 9; ++row) {
    for (int col = 0; col < 9; ++col) {
      char ch = '0';
      if (include_center && row == 4 && col == 4) {
        ch = '1';
      }
      if (row == 4 + dy && col == 4 + dx) {
        ch = '2';
      }
      if (duplicate && row == 4 && col == 6) {
        ch = '2';
      }
      text.push_back(ch);
    }
    if (row < 8) {
      text.push_back('\n');
    }
  }
  return text;
}

}  // namespace

TEST(RelativePosition, CenterOnly) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(0, 0), error);
  ASSERT_TRUE(cell.has_value()) << error;
  EXPECT_EQ(cell->first, 0);
  EXPECT_EQ(cell->second, 0);
}

TEST(RelativePosition, RightOffset) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(2, 0), error);
  ASSERT_TRUE(cell.has_value());
  EXPECT_EQ(cell->first, 2);
  EXPECT_EQ(cell->second, 0);
}

TEST(RelativePosition, LeftOffset) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(-2, 0), error);
  ASSERT_TRUE(cell.has_value());
  EXPECT_EQ(cell->first, -2);
  EXPECT_EQ(cell->second, 0);
}

TEST(RelativePosition, UpOffset) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(0, -2), error);
  ASSERT_TRUE(cell.has_value());
  EXPECT_EQ(cell->first, 0);
  EXPECT_EQ(cell->second, -2);
}

TEST(RelativePosition, DownOffset) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(0, 3), error);
  ASSERT_TRUE(cell.has_value());
  EXPECT_EQ(cell->first, 0);
  EXPECT_EQ(cell->second, 3);
}

TEST(RelativePosition, OverlapWithoutMarkerIsZero) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(0, 0, false), error);
  ASSERT_TRUE(cell.has_value()) << error;
  EXPECT_EQ(cell->first, 0);
  EXPECT_EQ(cell->second, 0);
}

TEST(RelativePosition, WrongRowCount) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position("000000000\n000000000", error);
  EXPECT_FALSE(cell.has_value());
  EXPECT_FALSE(error.empty());
}

TEST(RelativePosition, WrongRowLength) {
  std::string error;
  std::string text = make_grid(0, 0);
  text.pop_back();
  const auto cell = rmtc_bot::parse_relative_position(text, error);
  EXPECT_FALSE(cell.has_value());
  EXPECT_FALSE(error.empty());
}

TEST(RelativePosition, DuplicateMarker) {
  std::string error;
  const auto cell = rmtc_bot::parse_relative_position(make_grid(1, 1, true, true), error);
  EXPECT_FALSE(cell.has_value());
  EXPECT_FALSE(error.empty());
}

TEST(RelativePosition, IllegalCharacter) {
  std::string error;
  std::string text = make_grid(0, 0);
  text[0] = 'x';
  const auto cell = rmtc_bot::parse_relative_position(text, error);
  EXPECT_FALSE(cell.has_value());
  EXPECT_FALSE(error.empty());
}

TEST(RelativePosition, AbsoluteRestoresOffset) {
  std::string error;
  const auto cell = rmtc_bot::to_absolute(rmtc_bot::Cell{10, 7}, make_grid(1, -1), error);
  ASSERT_TRUE(cell.has_value()) << error;
  EXPECT_EQ(cell->first, 11);
  EXPECT_EQ(cell->second, 6);
}

TEST(RelativePosition, AbsoluteFailsOnBadText) {
  std::string error;
  const auto cell = rmtc_bot::to_absolute(rmtc_bot::Cell{1, 1}, "bad", error);
  EXPECT_FALSE(cell.has_value());
  EXPECT_FALSE(error.empty());
}
