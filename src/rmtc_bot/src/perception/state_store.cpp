#include "rmtc_bot/perception/state_store.hpp"

#include <sstream>

namespace rmtc_bot {

StateStore::StateStore() { reset(); }

void StateStore::reset() {
  session_id.clear();
  tick = -1;
  map_width = 0;
  map_height = 0;
  position = Cell{0, 0};
  hp = 0.0;
  alive = false;
  defending = false;
  protected_ = false;
  respawn_in = 0;
  team.clear();
  robot_id.clear();
  score = Score{};
  mission_text.clear();
  missions.clear();
  event_text.clear();
  vision_radius = 4;
  last_position_reason.reset();
  seen_ticks_.clear();
}

void StateStore::begin_session(const std::string& session) {
  reset();
  session_id = session;
}

void StateStore::update(const Perception& perception) {
  if (perception.session_id != session_id) {
    begin_session(perception.session_id);
  }
  tick = perception.tick;
  map_width = perception.map_width;
  map_height = perception.map_height;
  vision_radius = perception.vision_radius;
  position = perception.self.position;
  hp = perception.self.hp;
  alive = perception.self.alive;
  defending = perception.self.defending;
  protected_ = perception.self.protected_;
  respawn_in = perception.self.respawn_in;
  team = perception.self.team;
  robot_id = perception.self.robot_id;
  score = perception.score;
  mission_text = perception.mission_text;
  missions = perception.missions;
  event_text = perception.event_text;
  last_position_reason = perception.self.position_reason;
  seen_ticks_.insert(perception.tick);
}

bool StateStore::has_seen_tick(int seen_tick) const {
  return seen_ticks_.count(seen_tick) > 0;
}

std::set<std::string> StateStore::mission_completed_ids() const {
  return std::set<std::string>(score.mission_completed_ids.begin(),
                               score.mission_completed_ids.end());
}

bool StateStore::can_move() const { return alive; }

bool StateStore::can_attack(int stage) const {
  return alive && !protected_ && stage != 1;
}

bool StateStore::can_defend(int stage) const { return alive && stage != 1; }

bool StateStore::must_wait() const { return !alive; }

std::string StateStore::summary() const {
  std::ostringstream oss;
  oss << "tick=" << tick << " session=" << session_id << " pos=" << position
      << " hp=" << static_cast<int>(hp) << " alive=" << (alive ? "true" : "false")
      << " protected=" << (protected_ ? "true" : "false");
  return oss.str();
}

}  // namespace rmtc_bot
