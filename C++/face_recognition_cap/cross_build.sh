#!/bin/bash
# ==============================================================================
# RK3588 交叉编译脚本 (使用系统 Multi-arch 库)
# 用法:
#   ./cross_build.sh          # 增量编译 + 部署
#   ./cross_build.sh --deploy # 只部署，不编译
#   ./cross_build.sh --clean  # 清理后重新编译 + 部署
#   ./cross_build.sh --build  # 只编译，不部署
# ==============================================================================

set -e

# ==================== 解析命令行参数 ====================
DO_BUILD=true
DO_DEPLOY=true
DO_CLEAN=false

for arg in "$@"; do
    case $arg in
        --deploy)
            DO_BUILD=false
            DO_DEPLOY=true
            ;;
        --clean)
            DO_CLEAN=true
            ;;
        --build)
            DO_BUILD=true
            DO_DEPLOY=false
            ;;
        *)
            echo "未知参数: $arg"
            echo "用法: ./cross_build.sh [--deploy|--clean|--build]"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "RK3588 交叉编译 (x86_64 -> aarch64)"
echo "========================================"
echo "模式: 编译=$DO_BUILD, 部署=$DO_DEPLOY, 清理=$DO_CLEAN"

ROOT_PWD=$( cd "$( dirname "$0" )" && pwd )
BUILD_DIR=${ROOT_PWD}/build/build_cross_aarch64
INSTALL_DIR=${ROOT_PWD}/install

# ==================== 编译器设置 ====================
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++

echo "✓ 编译器: $($CXX --version | head -n1)"

# ==================== 路径配置 ====================
# aarch64 库路径 (apt 安装的)
AARCH64_LIB_DIR="/usr/lib/aarch64-linux-gnu"
AARCH64_INCLUDE_DIR="/usr/include/aarch64-linux-gnu"

# RKNN 和 RGA 路径 (这些是 RK3588 专有的，不在 apt 里)
RKNN_API_PATH="${ROOT_PWD}/../runtime/librknn_api"
RGA_PATH="${ROOT_PWD}/../3rdparty/rga"

echo "✓ OpenCV: ${AARCH64_LIB_DIR}/libopencv_core.so"
echo "✓ Qt5:    ${AARCH64_LIB_DIR}/cmake/Qt5"
echo "✓ RKNN:   ${RKNN_API_PATH}/aarch64/librknnrt.so"

# ==================== 编译逻辑 ====================
if [ "$DO_BUILD" = true ]; then
    # 清理构建目录（仅当 --clean 时）
    if [ "$DO_CLEAN" = true ]; then
        echo ""
        echo "🧹 清理构建目录..."
        rm -rf ${BUILD_DIR}
    fi
    
    mkdir -p ${BUILD_DIR}
    cd ${BUILD_DIR}

# ==================== CMake 配置 ====================
CMAKE_ARGS=(
    "../.."
    "-DCMAKE_SYSTEM_NAME=Linux"
    "-DCMAKE_SYSTEM_PROCESSOR=aarch64"
    "-DCMAKE_C_COMPILER=${CC}"
    "-DCMAKE_CXX_COMPILER=${CXX}"
    
    # 关键：指定 aarch64 库的查找路径
    "-DCMAKE_FIND_ROOT_PATH=${AARCH64_LIB_DIR}"
    "-DCMAKE_LIBRARY_PATH=${AARCH64_LIB_DIR}"
    
    # OpenCV - 使用系统安装的 arm64 版本
    "-DOpenCV_DIR=${AARCH64_LIB_DIR}/cmake/opencv4"
    
    # Qt5 - 使用系统安装的 arm64 版本
    "-DQt5_DIR=${AARCH64_LIB_DIR}/cmake/Qt5"
    "-DQt5Core_DIR=${AARCH64_LIB_DIR}/cmake/Qt5Core"
    "-DQt5Gui_DIR=${AARCH64_LIB_DIR}/cmake/Qt5Gui"
    "-DQt5Widgets_DIR=${AARCH64_LIB_DIR}/cmake/Qt5Widgets"
    "-DQt5Svg_DIR=${AARCH64_LIB_DIR}/cmake/Qt5Svg"
    "-DQt5Multimedia_DIR=${AARCH64_LIB_DIR}/cmake/Qt5Multimedia"
    "-DQt5Network_DIR=${AARCH64_LIB_DIR}/cmake/Qt5Network"
    
    # SQLite3
    "-DSQLite3_INCLUDE_DIR=/usr/include"
    "-DSQLite3_LIBRARY=${AARCH64_LIB_DIR}/libsqlite3.so"
    
    # 构建类型
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}/face_recognition_cap"
    
    # 编译标志
    "-DCMAKE_CXX_FLAGS=-I/usr/include -DSPDLOG_HEADER_ONLY -DFMT_HEADER_ONLY"
    "-DCMAKE_EXE_LINKER_FLAGS=-L${AARCH64_LIB_DIR} -Wl,-rpath-link,${AARCH64_LIB_DIR}"
)

echo ""
echo "CMake 配置..."
cmake "${CMAKE_ARGS[@]}"

# ==================== 编译 ====================
echo ""
echo "开始编译..."
NPROC=$(nproc)
make -j${NPROC}
make install

fi  # 结束 DO_BUILD 的 if 块

# ==================== 打包依赖库 ====================
cd ${ROOT_PWD}

if [ "$DO_BUILD" = true ]; then
    echo ""
echo "打包依赖库 (RKNN/RGA/RKLLM/spdlog/fmt，OpenCV/SQLite/Qt 使用系统库)..."
    LIB_DIR="${INSTALL_DIR}/face_recognition_cap/lib"
    mkdir -p "${LIB_DIR}"

    # 只复制 RKNN/RGA/RKLLM (这些是 RK3588 专有的)
    cp -f ${RKNN_API_PATH}/aarch64/librknnrt.so "${LIB_DIR}/" 2>/dev/null || true
    cp -f ${RGA_PATH}/lib/Linux/aarch64/librga.so "${LIB_DIR}/" 2>/dev/null || true
    cp -df ${ROOT_PWD}/../3rdparty/mpp/Linux/aarch64/librockchip_mpp.so* "${LIB_DIR}/" 2>/dev/null || true
    
    # RKLLM 运行时库 (本地 LLM 必需)
    RKLLM_LIB="${ROOT_PWD}/../rkllm_runtime/lib/librkllmrt.so"
    if [ -f "${RKLLM_LIB}" ]; then
        cp -f "${RKLLM_LIB}" "${LIB_DIR}/"
        echo "  ✓ RKNN/RGA/RKLLM 库已复制"
    else
        echo "  ⚠ RKLLM 库未找到: ${RKLLM_LIB}"
        echo "  ✓ RKNN/RGA 库已复制"
    fi

    # spdlog / fmt 运行时库（GUI 与 db_tool 依赖）
    cp -Lf ${AARCH64_LIB_DIR}/libspdlog.so.1 "${LIB_DIR}/" 2>/dev/null || true
    cp -Lf ${AARCH64_LIB_DIR}/libfmt.so.8 "${LIB_DIR}/" 2>/dev/null || true
    echo "  ✓ spdlog/fmt 库已复制"

    # 不复制 OpenCV/SQLite - 板子上已经有！

    # 不再生成运行脚本，按照用户要求移除
    # cat > "${INSTALL_DIR}/face_recognition_cap/remote_run.sh" << 'EOF'
    # ...
    # EOF


    echo ""
    echo "========================================"
    echo "✅ 编译完成！"
    echo "========================================"
    echo "安装目录: ${INSTALL_DIR}/face_recognition_cap"
fi

# ==================== 一键部署到设备 ====================
if [ "$DO_DEPLOY" = true ]; then
    DEVICE_IP_ETH="192.168.137.202"
    DEVICE_IP_WIFI="192.168.137.111"
    DEVICE_USER="elf"
    DEVICE_PASS="elf"
    DEVICE_TARGET_DIR="/home/elf/open_project/RK3588-NPU/C++/face_recognition_cap/install"

    # 根据连通性自动选择 IP（网线优先）
    if ping -c 1 -W 1 "${DEVICE_IP_ETH}" &>/dev/null; then
        DEVICE_IP="${DEVICE_IP_ETH}"
        echo "  网络: 网线 (${DEVICE_IP})"
    elif ping -c 1 -W 1 "${DEVICE_IP_WIFI}" &>/dev/null; then
        DEVICE_IP="${DEVICE_IP_WIFI}"
        echo "  网络: WiFi (${DEVICE_IP})"
    else
        echo "两个 IP 均不可达 (${DEVICE_IP_ETH} / ${DEVICE_IP_WIFI})，跳过部署。"
        exit 1
    fi

    # 检查是否安装了 sshpass
    if ! command -v sshpass &> /dev/null; then
        echo "⚠️  未检测到 sshpass，请运行 'sudo apt install sshpass' 以实现自动输入密码。"
        SSH_CMD="ssh -o StrictHostKeyChecking=no"
        SCP_CMD="scp -o StrictHostKeyChecking=no"
    else
        SSH_CMD="sshpass -p ${DEVICE_PASS} ssh -o StrictHostKeyChecking=no"
        SCP_CMD="sshpass -p ${DEVICE_PASS} scp -o StrictHostKeyChecking=no"
    fi

    echo ""
    echo "========================================"
    echo "🚀 开始部署到设备..."
    echo "========================================"
    # 仅确保目标目录存在，不再删除整个目录（以保留数据库和本地库文件）
    ${SSH_CMD} ${DEVICE_USER}@${DEVICE_IP} "mkdir -p ${DEVICE_TARGET_DIR}/face_recognition_cap/lib"

    # 传输核心可执行文件
    echo "  -> 传输核心可执行文件..."
    ${SCP_CMD} ${INSTALL_DIR}/face_recognition_cap/face_recognition_cap_gui \
        ${DEVICE_USER}@${DEVICE_IP}:${DEVICE_TARGET_DIR}/face_recognition_cap/
    if [ $? -eq 0 ]; then
        echo "     ✓ 核心可执行文件传输完成"
    else
        echo "     ✗ 核心可执行文件传输失败"
    fi

    # 传输运行时库 (RKNN/RGA/RKLLM) - 仅在设备缺失时传输
    echo "  -> 检查并传输运行时库 (RKNN/RGA/RKLLM)..."
    shopt -s nullglob
    for lib_path in ${INSTALL_DIR}/face_recognition_cap/lib/*.so*; do
        lib_name=$(basename "${lib_path}")
        if ${SSH_CMD} ${DEVICE_USER}@${DEVICE_IP} "[ -f ${DEVICE_TARGET_DIR}/face_recognition_cap/lib/${lib_name} ]"; then
            echo "     - 已存在: ${lib_name}"
        else
            echo "     - 传输: ${lib_name}"
            ${SCP_CMD} "${lib_path}" \
                ${DEVICE_USER}@${DEVICE_IP}:${DEVICE_TARGET_DIR}/face_recognition_cap/lib/
        fi
    done
    shopt -u nullglob

    if [ $? -eq 0 ]; then
        echo ""
        echo "========================================"
        echo "✅ 部署成功！"
        echo "========================================"
        echo ""
        echo "在设备上运行:"
        echo "  ssh ${DEVICE_USER}@${DEVICE_IP}"
        echo "  cd ${DEVICE_TARGET_DIR}/face_recognition_cap"
        echo "  export LD_LIBRARY_PATH=./lib:\$LD_LIBRARY_PATH"
        echo "  ./face_recognition_cap_gui   # GUI 版本"
    else
        echo ""
        echo "❌ 部署失败！请检查网络连接和 SSH 配置。"
        echo "手动部署:"
        echo "  scp -r ${INSTALL_DIR}/face_recognition_cap ${DEVICE_USER}@${DEVICE_IP}:${DEVICE_TARGET_DIR}/"
    fi
else
    echo ""
    echo "跳过部署 (使用 --deploy 参数单独部署)"
fi
