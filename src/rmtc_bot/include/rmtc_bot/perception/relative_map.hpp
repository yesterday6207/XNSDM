#pragma once

#include <optional>
#include <string>

#include "rmtc_bot/types.hpp"

namespace rmtc_bot {

/// 解析单个实体的 9x9 相对位置字符串，返回以本车为原点的 (dx, dy)。
/// 字符串总长 89（81 格 + 8 个换行），无末尾换行；重叠时无 '2' → (0,0)。
std::optional<Cell> parse_relative_position(const std::string& text, std::string& error);

/// 相对坐标恢复绝对坐标；失败返回 std::nullopt。
std::optional<Cell> to_absolute(const Cell& self_position, const std::string& text,
                                std::string& error);

}  // namespace rmtc_bot
