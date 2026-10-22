#include "rmtc_bot/types.hpp"

#include <sstream>

namespace rmtc_bot {

std::string cell_to_string(const Cell& cell) {
  std::ostringstream oss;
  oss << "(" << cell.first << "," << cell.second << ")";
  return oss.str();
}

}  // namespace rmtc_bot
