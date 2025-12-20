#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${ROOT_DIR}/install/face_recognition_cap/face_recognition_cap_gui"

if [[ ! -x "${BIN}" ]]; then
  echo "Executable not found: ${BIN}"
  echo "Run ./build.sh first."
  exit 1
fi

# Filter noisy rk-debug fence logs while keeping other output.
exec stdbuf -oL -eL "${BIN}" 2>&1 | grep -vF "rk-debug out_fence_fd = 0"
