#!/usr/bin/env bash
# RMTC 参赛程序启动脚本
# 用法: ./run_contestant.sh <席位 A/B/C/D> <队名> [网关地址/完整URL] [阶段 0/1/2]
# 示例: ./run_contestant.sh A '示例队伍' 192.168.1.100
#       ./run_contestant.sh A '示例队伍' http://127.0.0.1:8080/v1/chat/completions 2
#       RMTC_STAGE=1 ./run_contestant.sh A '示例队伍'
set -euo pipefail

TEAM="${1:-A}"
TEAM_NAME="${2:-RMTC-Bot}"
GATEWAY_IN="${3:-${RMTC_AI_BASE_URL:-http://127.0.0.1:8000}}"
STAGE="${4:-${RMTC_STAGE:-2}}"

# 未写协议时补全；已带端口或路径则只补协议，避免拼出 host:port:port
if [[ "$GATEWAY_IN" != http://* && "$GATEWAY_IN" != https://* ]]; then
  if [[ "$GATEWAY_IN" == *:* ]]; then
    GATEWAY_IN="http://${GATEWAY_IN}"
  else
    GATEWAY_IN="http://${GATEWAY_IN}:${RMTC_AI_PORT:-8000}"
  fi
fi

export ROS_DOMAIN_ID="${ROS_DOMAIN_ID:-27}"
export ROS_LOCALHOST_ONLY="${ROS_LOCALHOST_ONLY:-0}"
export RMW_IMPLEMENTATION="${RMW_IMPLEMENTATION:-rmw_fastrtps_cpp}"
export RMTC_AI_BASE_URL="$GATEWAY_IN"

if [[ -z "${RMTC_TEAM_TOKEN:-}" ]]; then
  echo "[WARN] RMTC_TEAM_TOKEN 未设置，AI 调用将被禁用，第二阶段任务分资格无法满足。" >&2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

find_ws_root() {
  local dir="$SCRIPT_DIR"
  while [[ "$dir" != "/" ]]; do
    if [[ -f "${dir}/install/setup.bash" ]]; then
      echo "$dir"
      return 0
    fi
    dir="$(dirname "$dir")"
  done
  return 1
}
WS_ROOT="$(find_ws_root || true)"

set +u
if [[ -f /opt/ros/humble/setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/ros/humble/setup.bash
fi
if [[ -n "${WS_ROOT}" && -f "${WS_ROOT}/install/setup.bash" ]]; then
  # shellcheck disable=SC1091
  source "${WS_ROOT}/install/setup.bash"
fi
set -u

exec ros2 run rmtc_bot main_node --ros-args \
  -p team:="${TEAM}" \
  -p team_name:="${TEAM_NAME}" \
  -p stage:="${STAGE}"
