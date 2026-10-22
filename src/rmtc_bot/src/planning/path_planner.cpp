#include "rmtc_bot/planning/path_planner.hpp"

#include <deque>
#include <utility>

namespace rmtc_bot {

const std::array<Direction, 4> kDirections{{
    {"up", 0, -1},
    {"down", 0, 1},
    {"left", -1, 0},
    {"right", 1, 0},
}};

std::string opposite_direction(const std::string& direction) {
  if (direction == "up") {
    return "down";
  }
  if (direction == "down") {
    return "up";
  }
  if (direction == "left") {
    return "right";
  }
  if (direction == "right") {
    return "left";
  }
  return direction;
}

Cell move_cell(const Cell& cell, const std::string& direction) {
  for (const auto& item : kDirections) {
    if (direction == item.name) {
      return Cell{cell.first + item.dx, cell.second + item.dy};
    }
  }
  return cell;
}

namespace {

bool in_bounds(const Cell& cell, int width, int height) {
  return cell.first >= 0 && cell.first < width && cell.second >= 0 && cell.second < height;
}

}  // namespace

CellIntMap bfs_distances(const Cell& start, int width, int height, const BlockedFn& blocked,
                         const CellSet& extra_blocked, int max_expansions) {
  CellIntMap distances;
  if (!in_bounds(start, width, height)) {
    return distances;
  }
  distances[start] = 0;
  std::deque<Cell> queue;
  queue.push_back(start);
  int expansions = 0;

  while (!queue.empty() && expansions < max_expansions) {
    const Cell cell = queue.front();
    queue.pop_front();
    ++expansions;
    for (const auto& direction : kDirections) {
      const Cell next{cell.first + direction.dx, cell.second + direction.dy};
      if (distances.count(next) > 0 || extra_blocked.count(next) > 0) {
        continue;
      }
      if (!in_bounds(next, width, height)) {
        continue;
      }
      if (blocked(next)) {
        continue;
      }
      distances[next] = distances[cell] + 1;
      queue.push_back(next);
    }
  }
  return distances;
}

std::optional<std::string> bfs_first_step(const Cell& start, const Cell& goal, int width,
                                          int height, const BlockedFn& blocked,
                                          const CellSet& extra_blocked, int max_expansions) {
  if (start == goal) {
    return std::nullopt;
  }
  if (!in_bounds(start, width, height) || !in_bounds(goal, width, height)) {
    return std::nullopt;
  }

  CellSet extra = extra_blocked;
  extra.erase(goal);

  std::unordered_map<Cell, std::pair<Cell, std::string>, CellHash> parents;
  CellSet visited;
  visited.insert(start);
  std::deque<Cell> queue;
  queue.push_back(start);
  int expansions = 0;
  bool found = false;

  while (!queue.empty() && expansions < max_expansions) {
    const Cell cell = queue.front();
    queue.pop_front();
    ++expansions;
    for (const auto& direction : kDirections) {
      const Cell next{cell.first + direction.dx, cell.second + direction.dy};
      if (visited.count(next) > 0 || extra.count(next) > 0) {
        continue;
      }
      if (!in_bounds(next, width, height)) {
        continue;
      }
      if (blocked(next)) {
        continue;
      }
      visited.insert(next);
      parents[next] = std::make_pair(cell, std::string(direction.name));
      if (next == goal) {
        found = true;
        queue.clear();
        break;
      }
      queue.push_back(next);
    }
  }

  if (!found) {
    return std::nullopt;
  }

  Cell cell = goal;
  std::string first_direction;
  while (cell != start) {
    const auto it = parents.find(cell);
    if (it == parents.end()) {
      return std::nullopt;
    }
    first_direction = it->second.second;
    cell = it->second.first;
  }
  return first_direction;
}

}  // namespace rmtc_bot
