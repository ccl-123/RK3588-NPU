set -e

GCC_COMPILER=aarch64-linux-gnu
AARCH64_LIB_DIR="/usr/lib/aarch64-linux-gnu"

#export LD_LIBRARY_PATH=${TOOL_CHAIN}/lib64:$LD_LIBRARY_PATH
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )

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

  if resolved="$(resolve_runtime_lib "${soname}")"; then
    cp -Lf "${resolved}" "${LIB_DIR}/"
    echo "✓ 已复制 ${soname} <- ${resolved}"
  else
    echo "⚠ 未找到运行时库: ${soname}"
  fi
}

# build
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
# 只在首次或缓存不存在时运行 cmake（支持增量编译）
if [[ ! -f "CMakeCache.txt" ]]; then
  cmake ../.. -DTARGET_NAME=face_recognition_cap
fi
make -j2
make install

# 打包运行时依赖，确保板端直接运行 install 目录中的 GUI 时能找到日志库。
LIB_DIR="${ROOT_PWD}/install/face_recognition_cap/lib"
mkdir -p "${LIB_DIR}"
copy_runtime_lib "libspdlog.so.1"
copy_runtime_lib "libfmt.so.8"

cd -
