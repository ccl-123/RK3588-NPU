cmake_minimum_required(VERSION 3.4.1)

project(${TARGET_NAME})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--allow-shlib-undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wl,--allow-shlib-undefined")

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/${TARGET_NAME})

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# rknn api
set(RKNN_API_PATH ${CMAKE_SOURCE_DIR}/../runtime/librknn_api)

if (CMAKE_C_COMPILER MATCHES "aarch64")
  set(LIB_ARCH aarch64)
else()
  set(LIB_ARCH armhf)
endif()
set(RKNN_RT_LIB ${RKNN_API_PATH}/${LIB_ARCH}/librknnrt.so)
include_directories(${RKNN_API_PATH}/include)
include_directories(${CMAKE_SOURCE_DIR}/../3rdparty)

# opencv
find_package(OpenCV REQUIRED)

#rga
set(RGA_PATH ${CMAKE_SOURCE_DIR}/../3rdparty/rga/)

if (CMAKE_C_COMPILER MATCHES "aarch64")
  set(LIB_ARCH aarch64)
else()
  set(LIB_ARCH armhf)
endif()
set(RGA_LIB ${RGA_PATH}/lib/Linux//${LIB_ARCH}/librga.so)
include_directories( ${RGA_PATH}/include)

set(CMAKE_INSTALL_RPATH "lib")

# demo - 分层架构
include_directories( ${CMAKE_SOURCE_DIR}/include)
include_directories( ${CMAKE_SOURCE_DIR}/include/core)
include_directories( ${CMAKE_SOURCE_DIR}/include/hardware)
include_directories( ${CMAKE_SOURCE_DIR}/include/utils)
include_directories( ${CMAKE_SOURCE_DIR}/include/app)

# 源文件分组
set(CORE_SOURCES
    src/core/retinaface.cc
    src/core/facenet.cc
    src/core/postprocess.cc
)

set(HARDWARE_SOURCES
    src/hardware/camera_util.cc
)

set(APP_SOURCES
    src/app/preprocessing_thread.cc
    src/app/rendering_thread.cc
    src/app/performance_monitor.cc
    src/app/model_manager.cc
    src/app/feature_library.cc
    src/app/face_recognition_app.cc
)

set(MAIN_SOURCES
    src/main.cc
)

add_executable(${TARGET_NAME}
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${HARDWARE_SOURCES}
    ${APP_SOURCES}
)

target_link_libraries(${TARGET_NAME}
  ${RKNN_RT_LIB}
  ${RGA_LIB}
  ${OpenCV_LIBS}
)

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/${TARGET_NAME})
install(TARGETS ${TARGET_NAME} DESTINATION ./)

install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
install(DIRECTORY data DESTINATION ./)
