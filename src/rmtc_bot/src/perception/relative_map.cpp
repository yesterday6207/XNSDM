#include "rmtc_bot/perception/relative_map.hpp"

#include <sstream>
#include <vector>

namespace rmtc_bot {

namespace {

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> rows;
  std::string current;
  for (const char ch : text) {
    if (ch == '\n') {
      rows.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }
  rows.push_back(current);
  return rows;
}

}  // namespace

std::optional<Cell> parse_relative_position(const std::string& text, std::string& error) {
  error.clear();
  const std::vector<std::string> rows = split_lines(text);
  if (rows.size() != 9) {
    std::ostringstream oss;
    oss << "相对位置图必须是 9 行，实际 " << rows.size() << " 行";
    error = oss.str();
    return std::nullopt;
  }
  for (const auto& row : rows) {
    if (row.size() != 9) {
      error = "相对位置图每行必须是 9 个字符";
      return std::nullopt;
    }
  }

  std::vector<Cell> found;
  for (int row_index = 0; row_index < 9; ++row_index) {
    for (int col_index = 0; col_index < 9; ++col_index) {
      const char ch = rows[static_cast<std::size_t>(row_index)][static_cast<std::size_t>(col_index)];
      if (ch == '2') {
        found.emplace_back(col_index, row_index);
      } else if (ch != '0' && ch != '1') {
        std::ostringstream oss;
        oss << "相对位置图包含非法字符: '" << ch << "'";
        error = oss.str();
        return std::nullopt;
      }
    }
  }

  if (found.empty()) {
    return Cell{0, 0};
  }
  if (found.size() > 1) {
    error = "相对位置图包含多个 '2'";
    return std::nullopt;
  }
  return Cell{found[0].first - 4, found[0].second - 4};
}

std::optional<Cell> to_absolute(const Cell& self_position, const std::string& text,
                                std::string& error) {
  const auto relative = parse_relative_position(text, error);
  if (!relative.has_value()) {
    return std::nullopt;
  }
  return Cell{self_position.first + relative->first, self_position.second + relative->second};
}

}  // namespace rmtc_bot
