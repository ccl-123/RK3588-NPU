cmake_minimum_required(VERSION 3.14)

project(${TARGET_NAME} LANGUAGES CXX C)

# ==================== C++ 标准设置（现代化） ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译器警告和链接选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--allow-shlib-undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--allow-shlib-undefined")

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/${TARGET_NAME})

set(CMAKE_SKIP_INSTALL_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# rknn api
set(RKNN_API_PATH ${CMAKE_SOURCE_DIR}/../runtime/librknn_api)

if (CMAKE_C_COMPILER MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
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

if (CMAKE_C_COMPILER MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  set(LIB_ARCH aarch64)
else()
  set(LIB_ARCH armhf)
endif()
set(RGA_LIB ${RGA_PATH}/lib/Linux/${LIB_ARCH}/librga.so)
include_directories( ${RGA_PATH}/include)

set(CMAKE_INSTALL_RPATH "lib")

# SQLite3
find_package(SQLite3 REQUIRED)
include_directories(${SQLite3_INCLUDE_DIRS})

# Qt5 (可选，用于 GUI 版本)
find_package(Qt5 COMPONENTS Core Gui Widgets Svg Multimedia)
if(Qt5_FOUND)
    message(STATUS "Qt5 found, GUI version will be built")
    message(STATUS "Qt5 version: ${Qt5_VERSION}")
else()
    message(STATUS "Qt5 not found, GUI version will be skipped")
endif()

# spdlog (header-only)
include_directories(/usr/local/include)

# demo - 分层架构
include_directories( ${CMAKE_SOURCE_DIR}/include)
include_directories( ${CMAKE_SOURCE_DIR}/include/core)
include_directories( ${CMAKE_SOURCE_DIR}/include/hardware)
include_directories( ${CMAKE_SOURCE_DIR}/include/utils)
include_directories( ${CMAKE_SOURCE_DIR}/include/app)
include_directories( ${CMAKE_SOURCE_DIR}/include/database)
include_directories( ${CMAKE_SOURCE_DIR}/include/service)
include_directories( ${CMAKE_SOURCE_DIR}/gui/include)

# 源文件分组
set(CORE_SOURCES
    src/core/yolov8_face.cc
    src/core/facenet.cc
    src/core/postprocess.cc
)

set(HARDWARE_SOURCES
    src/hardware/camera_util.cc
)

set(DATABASE_SOURCES
    src/database/database_manager.cc
    src/database/user_dao.cc
    src/database/face_feature_dao.cc
    src/database/attendance_record_dao.cc
)

set(SERVICE_SOURCES
    src/service/attendance_service.cc
    src/service/user_service.cc
)

set(APP_SOURCES
    src/app/preprocessing_thread.cc
    src/app/recognition_thread.cc
    src/app/performance_monitor.cc
    src/app/model_manager.cc
    src/app/feature_library.cc
    src/app/face_recognition_app.cc
)

set(MAIN_SOURCES
    src/main.cc
)

# GUI 源文件 (仅当 Qt5 可用时)
if(Qt5_FOUND)
    set(GUI_SOURCES
        gui/src/gui/main_window.cc
        gui/src/gui/video_display_widget.cc
        gui/src/gui/face_registration_dialog.cc
        gui/src/gui/user_edit_dialog.cc
        gui/src/gui/attendance_query_widget.cc
        gui/src/gui/user_management_widget.cc
        gui/src/gui/about_dialog.cc
        gui/src/themes/theme_manager.cc
        gui/src/utils/svg_icon_manager.cc
        gui/src/utils/stack_router.cc
        gui/src/utils/audio_manager.cc
        gui/src/utils/config_manager.cc
        gui/src/widgets/card_widget.cc
        gui/src/widgets/icon_button.cc
        gui/src/widgets/search_input.cc
        gui/src/widgets/status_tag.cc
        gui/src/widgets/status_item_delegate.cc
        gui/src/widgets/modern_table_view.cc
        gui/src/widgets/title_bar.cc
        gui/src/widgets/side_menu.cc
        gui/src/widgets/toast_notification.cc
        gui/src/ui/recognition_page.cc
        gui/src/ui/attendance_page.cc
        gui/src/ui/user_management_page.cc
        gui/src/ui/settings_page.cc
        gui/src/themes/ui_resources.qrc
        gui/resources/resources.qrc
    )

    set(MAIN_GUI_SOURCES
        src/main_gui.cc
    )
endif()

# 命令行版本
add_executable(${TARGET_NAME}
    ${MAIN_SOURCES}
    ${CORE_SOURCES}
    ${HARDWARE_SOURCES}
    ${DATABASE_SOURCES}
    ${SERVICE_SOURCES}
    ${APP_SOURCES}
)

target_link_libraries(${TARGET_NAME}
  ${RKNN_RT_LIB}
  ${RGA_LIB}
  ${OpenCV_LIBS}
  ${SQLite3_LIBRARIES}
)

# GUI 版本 (仅当 Qt5 可用时)
if(Qt5_FOUND)
    # GUI 头文件
    set(GUI_HEADERS
        gui/include/gui/main_window.h
        gui/include/gui/video_display_widget.h
        gui/include/gui/face_registration_dialog.h
        gui/include/gui/user_edit_dialog.h
        gui/include/gui/attendance_query_widget.h
        gui/include/gui/user_management_widget.h
        gui/include/gui/about_dialog.h
        gui/include/themes/theme_manager.h
        gui/include/utils/svg_icon_manager.h
        gui/include/utils/stack_router.h
        gui/include/utils/audio_manager.h
        gui/include/utils/config_manager.h
        gui/include/widgets/card_widget.h
        gui/include/widgets/icon_button.h
        gui/include/widgets/search_input.h
        gui/include/widgets/status_tag.h
        gui/include/widgets/status_item_delegate.h
        gui/include/widgets/modern_table_view.h
        gui/include/widgets/title_bar.h
        gui/include/widgets/side_menu.h
        gui/include/widgets/toast_notification.h
        gui/include/ui/recognition_page.h
        gui/include/ui/attendance_page.h
        gui/include/ui/user_management_page.h
        gui/include/ui/settings_page.h
    )

    add_executable(${TARGET_NAME}_gui
        ${MAIN_GUI_SOURCES}
        ${CORE_SOURCES}
        ${HARDWARE_SOURCES}
        ${DATABASE_SOURCES}
        ${SERVICE_SOURCES}
        ${APP_SOURCES}
        ${GUI_SOURCES}
        ${GUI_HEADERS}
    )

    # 为 GUI 目标启用 AUTOMOC
    set_target_properties(${TARGET_NAME}_gui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )

    target_link_libraries(${TARGET_NAME}_gui
        ${RKNN_RT_LIB}
        ${RGA_LIB}
        ${OpenCV_LIBS}
        ${SQLite3_LIBRARIES}
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Svg
        Qt5::Multimedia
    )

    # 安装 GUI 版本
    install(TARGETS ${TARGET_NAME}_gui DESTINATION ./)
endif()

# 数据库管理工具
add_executable(db_tool
    tools/db_tool.cc
    ${DATABASE_SOURCES}
    ${SERVICE_SOURCES}
    src/app/feature_library.cc
    src/core/postprocess.cc
)

target_link_libraries(db_tool
  ${OpenCV_LIBS}
  ${SQLite3_LIBRARIES}
)

# install target and libraries
set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install/${TARGET_NAME})
install(TARGETS ${TARGET_NAME} DESTINATION ./)
install(TARGETS db_tool DESTINATION ./)

install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
install(DIRECTORY data DESTINATION ./)
