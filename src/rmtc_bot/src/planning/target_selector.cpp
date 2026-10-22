#include "rmtc_bot/planning/target_selector.hpp"

#include <limits>
#include <vector>

namespace rmtc_bot {

std::optional<ResourceChoice> pick_resource(const Cell& start,
                                            const std::vector<ResourceTrack>& resources,
                                            int width, int height, const BlockedFn& blocked,
                                            const std::string& prefer,
                                            const CellSet& extra_blocked,
                                            bool tour_lookahead) {
  if (resources.empty()) {
    return std::nullopt;
  }
  const CellIntMap distances = bfs_distances(start, width, height, blocked, extra_blocked);

  auto value_matches = [&prefer](int value) {
    if (prefer == "high") {
      return value >= 20;
    }
    if (prefer == "normal") {
      return value < 20;
    }
    return true;
  };

  if (tour_lookahead) {
    // 以“先去哪颗资源、再贪心串完其余资源”的总路程评估，避免只盯着最近一颗。
    std::vector<const ResourceTrack*> nodes;
    std::vector<int> first_distances;
    for (const auto& track : resources) {
      const auto found = distances.find(track.position);
      if (track.position == start || found == distances.end()) {
        continue;
      }
      nodes.push_back(&track);
      first_distances.push_back(found->second);
    }
    if (nodes.empty()) {
      return std::nullopt;
    }

    const int count = static_cast<int>(nodes.size());
    std::vector<CellIntMap> maps;
    maps.reserve(count);
    for (const auto* node : nodes) {
      maps.push_back(bfs_distances(node->position, width, height, blocked, extra_blocked));
    }
    std::vector<std::vector<int>> matrix(count, std::vector<int>(count, -1));
    for (int i = 0; i < count; ++i) {
      for (int j = 0; j < count; ++j) {
        const auto found = maps[i].find(nodes[j]->position);
        matrix[i][j] = found == maps[i].end() ? -1 : found->second;
      }
    }

    int best_index = -1;
    long best_total = std::numeric_limits<long>::max();
    auto evaluate = [&](int candidate) {
      long total = first_distances[candidate];
      std::vector<char> used(count, 0);
      used[candidate] = 1;
      int current = candidate;
      for (int step = 1; step < count; ++step) {
        int next = -1;
        int next_distance = std::numeric_limits<int>::max();
        for (int j = 0; j < count; ++j) {
          if (used[j] != 0 || matrix[current][j] < 0) {
            continue;
          }
          if (matrix[current][j] < next_distance) {
            next_distance = matrix[current][j];
            next = j;
          }
        }
        if (next < 0) {
          total += 1000L * (count - step);  // 有资源互相不可达：重罚，避免把路线带死
          break;
        }
        total += next_distance;
        used[next] = 1;
        current = next;
      }
      const bool better =
          best_index < 0 || total < best_total ||
          (total == best_total &&
           (nodes[candidate]->value > nodes[best_index]->value ||
            (nodes[candidate]->value == nodes[best_index]->value &&
             nodes[candidate]->resource_id < nodes[best_index]->resource_id)));
      if (better) {
        best_index = candidate;
        best_total = total;
      }
    };

    bool matched = false;
    for (int i = 0; i < count; ++i) {
      if (value_matches(nodes[i]->value)) {
        evaluate(i);
        matched = true;
      }
    }
    if (!matched) {
      for (int i = 0; i < count; ++i) {
        evaluate(i);
      }
    }
    if (best_index < 0) {
      return std::nullopt;
    }

    ResourceChoice choice;
    choice.resource_id = nodes[best_index]->resource_id;
    choice.position = nodes[best_index]->position;
    choice.value = nodes[best_index]->value;
    choice.distance = first_distances[best_index];
    choice.step = bfs_first_step(start, choice.position, width, height, blocked, extra_blocked);
    return choice;
  }

  const ResourceTrack* best = nullptr;
  int best_distance = 0;
  auto consider = [&](const ResourceTrack& track) {
    if (track.position == start) {
      return;
    }
    const auto found = distances.find(track.position);
    if (found == distances.end()) {
      return;
    }
    const bool better =
        best == nullptr || found->second < best_distance ||
        (found->second == best_distance && track.value > best->value) ||
        (found->second == best_distance && track.value == best->value &&
         track.resource_id < best->resource_id);
    if (better) {
      best = &track;
      best_distance = found->second;
    }
  };

  for (const auto& track : resources) {
    if (value_matches(track.value)) {
      consider(track);
    }
  }
  if (best == nullptr && prefer != "any") {
    for (const auto& track : resources) {
      consider(track);
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }

  ResourceChoice choice;
  choice.resource_id = best->resource_id;
  choice.position = best->position;
  choice.value = best->value;
  choice.distance = best_distance;
  choice.step = bfs_first_step(start, best->position, width, height, blocked, extra_blocked);
  return choice;
}

std::optional<std::pair<Cell, std::optional<std::string>>> pick_frontier(
    const Cell& start, const CellSet& seen, int width, int height, const BlockedFn& blocked,
    const CellSet& extra_blocked) {
  const CellIntMap distances = bfs_distances(start, width, height, blocked, extra_blocked);
  std::optional<Cell> best_cell;
  int best_distance = std::numeric_limits<int>::max();
  for (const auto& item : distances) {
    if (seen.count(item.first) > 0) {
      continue;
    }
    const bool better = !best_cell.has_value() || item.second < best_distance ||
                        (item.second == best_distance && item.first < *best_cell);
    if (better) {
      best_cell = item.first;
      best_distance = item.second;
    }
  }
  if (!best_cell.has_value()) {
    return std::nullopt;
  }
  const auto step =
      bfs_first_step(start, *best_cell, width, height, blocked, extra_blocked);
  return std::make_pair(*best_cell, step);
}

}  // namespace rmtc_bot
