#!/usr/bin/env bash
# device_build.sh — ELF RK3588 开发板本地构建（Ubuntu 22.04 aarch64）
# 在板端编译并安装到 install/face_recognition_cap/；PC 交叉编译请用 cross_build.sh

set -euo pipefail

GCC_COMPILER=${GCC_COMPILER:-aarch64-linux-gnu}
AARCH64_LIB_DIR="/usr/lib/aarch64-linux-gnu"
BUILD_JOBS=${BUILD_JOBS:-2}

if [[ "$(uname -m)" == "aarch64" ]]; then
  export CC=${CC:-gcc}
  export CXX=${CXX:-g++}
else
  export CC=${CC:-${GCC_COMPILER}-gcc}
  export CXX=${CXX:-${GCC_COMPILER}-g++}
fi

ROOT_PWD="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

resolve_runtime_lib() {
  local soname="$1"
  local candidate=""

  for candidate in \
    "${AARCH64_LIB_DIR}/${soname}" \
    "/lib/aarch64-linux-gnu/${soname}" \
    "/usr/lib/${soname}" \
    "/lib/${soname}"; do
    if [[ -f "${candidate}" ]]; then
      echo "${candidate}"
      return 0
    fi
  done

  candidate="$(ldconfig -p 2>/dev/null | awk -v name="${soname}" '$1 == name { print $NF; exit }')"
  if [[ -n "${candidate}" && -f "${candidate}" ]]; then
    echo "${candidate}"
    return 0
  fi

  return 1
}

copy_runtime_lib() {
  local soname="$1"
  local resolved=""
  local target="${LIB_DIR}/${soname}"

  if [[ -e "${target}" || -L "${target}" ]]; then
    echo "✓ ${soname} 已存在，跳过复制: ${target}"
    return 0
  fi

  if resolved="$(resolve_runtime_lib "${soname}")"; then
    cp -Lf "${resolved}" "${LIB_DIR}/"
    echo "✓ 已复制 ${soname} <- ${resolved}"
  else
    echo "⚠ 未找到运行时库: ${soname}"
  fi
}

# build
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64
INSTALL_DIR=${ROOT_PWD}/install/face_recognition_cap

mkdir -p "${BUILD_DIR}"

cmake -S "${ROOT_PWD}" -B "${BUILD_DIR}" \
  -DTARGET_NAME=face_recognition_cap \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"

cmake --build "${BUILD_DIR}" --target face_recognition_cap_gui --parallel "${BUILD_JOBS}"

if [[ ! -x "${INSTALL_DIR}/db_tool" ]]; then
  echo "未找到已安装的 db_tool，开始构建..."
  cmake --build "${BUILD_DIR}" --target db_tool --parallel "${BUILD_JOBS}"
else
  echo "db_tool 已存在，跳过构建: ${INSTALL_DIR}/db_tool"
fi

cmake --install "${BUILD_DIR}"

# 打包运行时依赖，确保板端直接运行 install 目录中的 GUI 时能找到日志库。
LIB_DIR="${INSTALL_DIR}/lib"
mkdir -p "${LIB_DIR}"
copy_runtime_lib "libspdlog.so.1"
copy_runtime_lib "libfmt.so.8"
