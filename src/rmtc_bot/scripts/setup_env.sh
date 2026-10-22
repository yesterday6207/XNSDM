#!/usr/bin/env bash
# RMTC 参赛程序首次环境配置脚本
# 用法: ./setup_env.sh [--no-apt] [--skip-test]
#   --no-apt     跳过系统依赖安装（deps 已就绪或无法使用 apt 时）
#   --skip-test  跳过 colcon test，只构建
set -euo pipefail

SCRIPT_PATH="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd)"
WS_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

DO_APT=1
DO_TEST=1
for arg in "$@"; do
  case "${arg}" in
    --no-apt) DO_APT=0 ;;
    --skip-test) DO_TEST=0 ;;
    -h|--help)
      echo "用法: $0 [--no-apt] [--skip-test]"
      exit 0
      ;;
    *)
      echo "[ERROR] 未知参数: ${arg}" >&2
      echo "用法: $0 [--no-apt] [--skip-test]" >&2
      exit 2
      ;;
  esac
done

log() { echo "[setup] $*"; }
warn() { echo "[WARN] $*" >&2; }
die() { echo "[ERROR] $*" >&2; exit 1; }

# ---------- 1. 基础环境检查 ----------
if [[ ! -f /etc/os-release ]]; then
  die "无法识别操作系统，本脚本仅支持 Ubuntu 22.04（ROS 2 Humble）。"
fi
# shellcheck disable=SC1091
. /etc/os-release
if [[ "${VERSION_ID:-}" != "22.04" ]]; then
  warn "当前系统为 ${PRETTY_NAME:-未知}，本项目按 Ubuntu 22.04 + ROS 2 Humble 验证。"
  warn "若继续使用其它发行版，请自行保证 ROS 2 Humble 的 ABI/依赖兼容。"
fi

if [[ "${EUID}" -eq 0 ]]; then
  SUDO=""
elif command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

[[ -d "${WS_ROOT}/src/rmtc_bot" ]] || die "未找到工作区源码: ${WS_ROOT}/src/rmtc_bot"

APT_PKGS=(
  build-essential
  cmake
  git
  curl
  libjsoncpp-dev
  libyaml-cpp-dev
  libcurl4-openssl-dev
  libcurl4
  python3-colcon-common-extensions
  python3-rosdep
  ros-humble-ros-base
  ros-humble-rmw-fastrtps-cpp
  ros-humble-ament-cmake-gtest
  ros-humble-ament-index-cpp
  ros-humble-launch-ros
  ros-humble-ros2launch
)

pkg_installed() {
  dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"
}

setup_ros_apt_repo() {
  log "配置 ROS 2 apt 软件源"
  ${SUDO} apt-get update
  ${SUDO} apt-get install -y curl gnupg lsb-release
  ${SUDO} curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
    -o /usr/share/keyrings/ros-archive-keyring.gpg
  local codename
  codename="$(. /etc/os-release && echo "${UBUNTU_CODENAME:-jammy}")"
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu ${codename} main" \
    | ${SUDO} tee /etc/apt/sources.list.d/ros2.list >/dev/null
  ${SUDO} apt-get update
}

# ---------- 2. 安装系统依赖 ----------
if [[ "${DO_APT}" -eq 1 ]]; then
  command -v apt-get >/dev/null 2>&1 || die "未找到 apt-get，请用 --no-apt 跳过依赖安装并手动准备环境。"
  if [[ -z "${SUDO}" && "${EUID}" -ne 0 ]]; then
    die "安装系统依赖需要 sudo 权限。请使用管理员账户运行，或先手动安装依赖后加 --no-apt。"
  fi

  missing=()
  for pkg in "${APT_PKGS[@]}"; do
    if ! pkg_installed "${pkg}"; then
      missing+=("${pkg}")
    fi
  done

  if [[ ${#missing[@]} -gt 0 ]]; then
    log "缺少系统依赖: ${missing[*]}"
    if ! grep -qs "packages.ros.org" /etc/apt/sources.list /etc/apt/sources.list.d/*.list \
        /etc/apt/sources.list.d/*.sources 2>/dev/null; then
      setup_ros_apt_repo
    else
      ${SUDO} apt-get update
    fi
    ${SUDO} apt-get install -y "${missing[@]}"
  else
    log "系统依赖已齐全，跳过 apt 安装"
  fi
else
  log "按参数要求跳过系统依赖安装（--no-apt）"
fi

# ---------- 3. rosdep 初始化（尽力而为） ----------
if command -v rosdep >/dev/null 2>&1; then
  if [[ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
    if ! ${SUDO} rosdep init >/dev/null 2>&1; then
      warn "rosdep init 失败（可能已初始化或无权限），继续执行。"
    fi
  fi
  if ! rosdep update >/dev/null 2>&1; then
    warn "rosdep update 失败（通常是网络问题），本项目无额外 rosdep 依赖，继续执行。"
  fi
else
  warn "未找到 rosdep，跳过初始化；不影响本项目构建。"
fi

# ---------- 4. 加载 ROS 环境 ----------
set +u
if [[ -f /opt/ros/humble/setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/ros/humble/setup.bash
else
  set -u
  die "未找到 /opt/ros/humble/setup.bash，请确认 ROS 2 Humble 已安装（apt install ros-humble-ros-base）。"
fi
set -u
command -v colcon >/dev/null 2>&1 || die "未找到 colcon，请安装 python3-colcon-common-extensions。"
command -v ros2 >/dev/null 2>&1 || die "未找到 ros2 命令，请检查 ROS 2 Humble 安装。"

# ---------- 5. 构建工作区 ----------
export PYTHONDONTWRITEBYTECODE=1
cd "${WS_ROOT}"
log "构建工作区: ${WS_ROOT}"
if ! colcon build --packages-select rmtc_bot --cmake-args -DCMAKE_BUILD_TYPE=Release; then
  die "构建失败。请查看上方日志；如需彻底重建，可手动删除 build/ install/ 后重试。"
fi
[[ -x "${WS_ROOT}/install/rmtc_bot/lib/rmtc_bot/main_node" ]] \
  || die "构建完成但未找到 main_node 可执行文件，请检查 CMakeLists.txt 安装规则。"

# ---------- 6. 单元测试 ----------
if [[ "${DO_TEST}" -eq 1 ]]; then
  log "运行单元测试"
  if ! colcon test --packages-select rmtc_bot; then
    die "colcon test 执行失败，请查看上方日志。"
  fi
  if ! colcon test-result --verbose; then
    die "存在失败的测试用例，请查看上方输出。"
  fi
fi

# ---------- 7. 完成提示 ----------
cat <<'EOF'

[setup] 环境配置完成。

首次使用请先加载工作区环境：
  source /opt/ros/humble/setup.bash
  source install/setup.bash

启动参赛程序（A 为席位，按裁判分配选择 A/B/C/D）：
  export RMTC_TEAM_TOKEN="<本场席位令牌>"
  ./run_contestant.sh A 队伍名称 <网关地址>

公开测试端（测试裁判）联调示例：
  export RMTC_TEAM_TOKEN=practice-A
  ./run_contestant.sh A 队伍名称 http://127.0.0.1:8080/v1/chat/completions 2

第一阶段练习把最后的阶段参数改为 1。

也可直接：
  ./run_contestant.sh A 队伍名称 <网关地址>
（未设置 RMTC_TEAM_TOKEN 时会告警并关闭 AI，仅使用本地策略。）
EOF
