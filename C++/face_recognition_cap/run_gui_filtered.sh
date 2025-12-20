#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/install/face_recognition_cap/face_recognition_cap_gui"

if [[ ! -x "${BIN}" ]]; then
  echo "Executable not found: ${BIN}"
  echo "Run ./build.sh first."
  exit 1
fi

# Filter noisy rk-debug fence logs while keeping ANSI colors.
exec script -q /dev/null -c "${BIN}" 2>&1 | grep --line-buffered -vF "rk-debug out_fence_fd = 0"
