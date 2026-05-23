#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/install/face_recognition_cap/face_recognition_cap_gui"
LIB_DIR="${ROOT_DIR}/install/face_recognition_cap/lib"

# Run high-performance locking script (requires sudo)
if [[ -f "${ROOT_DIR}/fix_freq_rk3588.sh" ]]; then
  echo "Applying RK3588 full-performance configurations (requires sudo)..."
  sudo bash "${ROOT_DIR}/fix_freq_rk3588.sh"
fi

if [[ ! -x "${BIN}" ]]; then
  echo "Executable not found: ${BIN}"
  echo "Run ./build.sh first."
  exit 1
fi

for lib in libspdlog.so.1 libfmt.so.8; do
  if [[ ! -f "${LIB_DIR}/${lib}" ]]; then
    echo "Missing bundled runtime library: ${LIB_DIR}/${lib}"
    echo "Run ./build.sh to refresh install/face_recognition_cap/lib."
    exit 1
  fi
done

# Make bundled runtime dependencies visible for both direct and transitive shared-library loads.
export LD_LIBRARY_PATH="${LIB_DIR}:${LD_LIBRARY_PATH:-}"

# Filter noisy rk-debug fence logs while keeping ANSI colors.
exec script -q /dev/null -c "${BIN}" 2>&1 | grep --line-buffered -vF "rk-debug out_fence_fd = 0"
