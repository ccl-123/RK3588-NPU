set -e

GCC_COMPILER=aarch64-linux-gnu
AARCH64_LIB_DIR="/usr/lib/aarch64-linux-gnu"

#export LD_LIBRARY_PATH=${TOOL_CHAIN}/lib64:$LD_LIBRARY_PATH
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )

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
cp -Lf ${AARCH64_LIB_DIR}/libspdlog.so.1 "${LIB_DIR}/" 2>/dev/null || true
cp -Lf ${AARCH64_LIB_DIR}/libfmt.so.8 "${LIB_DIR}/" 2>/dev/null || true

cd -
