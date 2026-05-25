#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/install/face_recognition_cap/face_recognition_cap_gui"
LIB_DIR="${ROOT_DIR}/install/face_recognition_cap/lib"

if [[ ! -x "${BIN}" ]]; then
  echo "Executable not found: ${BIN}"
  echo "Run ./device_build.sh first."
  exit 1
fi

for lib in libspdlog.so.1 libfmt.so.8; do
  if [[ ! -f "${LIB_DIR}/${lib}" ]]; then
    echo "Missing bundled runtime library: ${LIB_DIR}/${lib}"
    echo "Run ./device_build.sh to refresh install/face_recognition_cap/lib."
    exit 1
  fi
done

# Make bundled runtime dependencies visible for both direct and transitive shared-library loads.
export LD_LIBRARY_PATH="${LIB_DIR}:${LD_LIBRARY_PATH:-}"

# SSH/Cursor：补 DISPLAY；本机桌面终端已有 DISPLAY 时不会改动
# shellcheck source=scripts/setup_display_env.sh
source "${ROOT_DIR}/scripts/setup_display_env.sh"
setup_session_env

if [[ -z "${DISPLAY:-}" ]]; then
  echo "错误: 当前会话无法连接已登录桌面的 X11 显示。"
  echo "  SSH 启动前，请在设备桌面终端执行: xhost +SI:localuser:$(id -un)"
  echo "  或在 SSH 中导出桌面会话使用的 DISPLAY/XAUTHORITY。"
  exit 1
fi

if ! timeout 2s xdpyinfo -display "${DISPLAY}" >/dev/null 2>&1; then
  echo "错误: 无法连接显示 ${DISPLAY}（X11 认证失败或显示无响应）。"
  echo "  当前 X socket: $(ls /tmp/.X11-unix/ 2>/dev/null | tr '\n' ' ')"
  exit 1
fi

# Run high-performance locking only after GUI display access has been confirmed.
if [[ -f "${ROOT_DIR}/fix_freq_rk3588.sh" ]]; then
  echo "Applying RK3588 full-performance configurations (requires sudo)..."
  sudo bash "${ROOT_DIR}/fix_freq_rk3588.sh"
fi

# Filter noisy rk-debug fence logs while keeping ANSI colors.
exec script -q /dev/null -c "${BIN}" 2>&1 | grep --line-buffered -vF "rk-debug out_fence_fd = 0"
