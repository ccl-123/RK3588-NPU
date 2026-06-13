cmake_minimum_required(VERSION 3.14)

set(TARGET_NAME face_recognition_cap)
project(${TARGET_NAME} LANGUAGES CXX C)

# ==================== C++ 标准设置（现代化） ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译告警开关
option(ENABLE_PROJECT_WARNINGS "Enable common compiler warnings" ON)
option(ENABLE_STRICT_PEDANTIC "Enable -Wpedantic (may conflict with third-party headers)" OFF)

function(enable_target_warnings target_name)
    if(NOT ENABLE_PROJECT_WARNINGS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra)
        if(ENABLE_STRICT_PEDANTIC)
            target_compile_options(${target_name} PRIVATE -Wpedantic)
        endif()
    endif()
endfunction()

# 编译器警告和链接选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,--allow-shlib-undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--allow-shlib-undefined")

# ==================== RPATH 设置 (工程化做法) ====================
# 让程序启动时自动在同级 lib 目录查找库文件 ($ORIGIN 在 Linux 上代表程序所在目录)
if(UNIX)
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
endif()

# 统一默认安装前缀；外部显式传入 CMAKE_INSTALL_PREFIX 时不覆盖。
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT OR CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install/${TARGET_NAME}"
        CACHE PATH "Install prefix" FORCE)
endif()



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

# rkllm api
set(RKLLM_API_PATH ${CMAKE_SOURCE_DIR}/../rkllm_runtime)
include_directories(${RKLLM_API_PATH}/include)
set(RKLLM_RT_LIB ${RKLLM_API_PATH}/lib/librkllmrt.so)

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

#mpp
set(MPP_PATH ${CMAKE_SOURCE_DIR}/../3rdparty/mpp/)
set(MPP_LIB ${MPP_PATH}/Linux/${LIB_ARCH}/librockchip_mpp.so)
include_directories( ${MPP_PATH}/include/rockchip)

# SQLite3
find_package(SQLite3 REQUIRED)
include_directories(${SQLite3_INCLUDE_DIRS})

# spdlog / fmt
find_package(PkgConfig REQUIRED)
pkg_check_modules(SPDLOG REQUIRED spdlog)
pkg_check_modules(FMT REQUIRED fmt)

# Qt5 (可选，用于 GUI 版本)
find_package(Qt5 COMPONENTS Core Gui Widgets Svg Multimedia Network Concurrent)
if(Qt5_FOUND)
    message(STATUS "Qt5 found, GUI version will be built")
    message(STATUS "Qt5 version: ${Qt5_VERSION}")
else()
    message(STATUS "Qt5 not found, GUI version will be skipped")
endif()

function(link_logging_dependencies target_name)
    target_include_directories(${target_name} PRIVATE
        ${SPDLOG_INCLUDE_DIRS}
        ${FMT_INCLUDE_DIRS}
    )
    target_compile_definitions(${target_name} PRIVATE
        SPDLOG_HEADER_ONLY
        SPDLOG_FMT_EXTERNAL
    )
    target_compile_options(${target_name} PRIVATE
        ${FMT_CFLAGS}
    )
    target_link_directories(${target_name} PRIVATE
        ${FMT_LIBRARY_DIRS}
    )
    target_link_libraries(${target_name}
        ${FMT_LIBRARIES}
    )
    target_link_options(${target_name} PRIVATE
        ${FMT_LDFLAGS_OTHER}
    )
endfunction()

# demo - 分层架构
include_directories( ${CMAKE_SOURCE_DIR}/include)
include_directories( ${CMAKE_SOURCE_DIR}/include/core)
include_directories( ${CMAKE_SOURCE_DIR}/include/hardware)
include_directories( ${CMAKE_SOURCE_DIR}/include/utils)
include_directories( ${CMAKE_SOURCE_DIR}/include/app)
include_directories( ${CMAKE_SOURCE_DIR}/include/database)
include_directories( ${CMAKE_SOURCE_DIR}/include/service)
include_directories( ${CMAKE_SOURCE_DIR}/include/agent)
include_directories( ${CMAKE_SOURCE_DIR}/include/tools)
include_directories( ${CMAKE_SOURCE_DIR}/include/gui_services)
include_directories( ${CMAKE_SOURCE_DIR}/include/gui_utils)
include_directories( ${CMAKE_SOURCE_DIR}/gui/include)

# 源文件分组
set(CORE_SOURCES
    src/core/yolov8_face.cc
    src/core/facenet.cc
    src/core/postprocess.cc
)

set(HARDWARE_SOURCES
    src/hardware/camera_util.cc
    src/hardware/camera_usb.cc
    src/hardware/camera_mipi.cc
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

# Agent 模块源文件
set(AGENT_SOURCES
    src/agent/tool_registry.cc
    src/agent/tool_executor.cc
    src/agent/conversation_memory.cc
    src/agent/incremental_response_parser.cc
    src/agent/react_agent.cc
    src/agent/agent_service.cc
    src/agent/agent_worker.cc
)

# Agent 工具源文件
set(TOOLS_SOURCES
    src/tools/attendance_tool.cc
    src/tools/department_attendance_tool.cc
    src/tools/attendance_ranking_tool.cc
    src/tools/missing_attendance_tool.cc
    src/tools/user_attendance_tool.cc
    src/tools/user_tool.cc
    src/tools/system_tool.cc
)

set(APP_SOURCES
    src/app/preprocessing_thread.cc
    src/app/postprocess_thread.cc
    src/app/recognition_thread.cc
    src/app/performance_monitor.cc
    src/app/model_manager.cc
    src/app/feature_library.cc
    src/app/npu_resource_manager.cc
    src/app/face_recognition_app.cc
    # local_llm_thread.cc 移到 GUI_SOURCES，因为依赖 Qt
)

# GUI 源文件 (仅当 Qt5 可用时)
if(Qt5_FOUND)
    # GUI 服务和工具类（已移至主项目目录）
    set(GUI_SERVICES_SOURCES
        src/gui_services/weather_service.cc
        src/gui_services/holiday_service.cc
        src/gui_services/news_service.cc
        src/gui_services/ai_analysis_service.cc
        src/gui_services/local_ai_analysis_service.cc
        src/gui_services/llm_protocol_adapter.cc
        src/gui_services/asr_service.cc
    )

    set(GUI_UTILS_SOURCES
        src/gui_utils/svg_icon_manager.cc
        src/gui_utils/stack_router.cc
        src/gui_utils/audio_manager.cc
        src/gui_utils/config_manager.cc
    )

    set(GUI_SOURCES
        gui/src/gui/main_window.cc
        gui/src/gui/video_display_widget.cc
        gui/src/gui/face_registration_dialog.cc
        gui/src/gui/user_edit_dialog.cc
        gui/src/gui/about_dialog.cc
        gui/src/themes/theme_manager.cc
        gui/src/widgets/card_widget.cc
        gui/src/widgets/icon_button.cc
        gui/src/widgets/search_input.cc
        gui/src/widgets/status_tag.cc
        gui/src/widgets/status_item_delegate.cc
        gui/src/widgets/attendance_list_widget.cc
        gui/src/widgets/modern_table_view.cc
        gui/src/widgets/title_bar.cc
        gui/src/widgets/news_ticker.cc
        gui/src/widgets/side_menu.cc
        gui/src/widgets/toast_notification.cc
        gui/src/ui/recognition_page.cc
        gui/src/ui/dashboard_page.cc
        gui/src/ui/attendance_page.cc
        gui/src/ui/user_management_page.cc
        gui/src/ui/settings_page.cc
        src/app/local_llm_thread.cc
        gui/src/themes/ui_resources.qrc
        gui/resources/resources.qrc
    )

    set(MAIN_GUI_SOURCES
        src/main_gui.cc
    )
endif()

# GUI 版本 (仅当 Qt5 可用时)
if(Qt5_FOUND)
    # GUI 头文件
    set(GUI_HEADERS
        gui/include/gui/main_window.h
        gui/include/gui/video_display_widget.h
        gui/include/gui/face_registration_dialog.h
        gui/include/gui/user_edit_dialog.h
        gui/include/gui/about_dialog.h
        include/gui_services/weather_service.h
        include/gui_services/holiday_service.h
        include/gui_services/news_service.h
        include/gui_services/ai_analysis_service.h
        include/gui_services/local_ai_analysis_service.h
        include/gui_services/asr_service.h
        gui/include/themes/theme_manager.h
        include/gui_utils/svg_icon_manager.h
        include/gui_utils/stack_router.h
        include/gui_utils/audio_manager.h
        include/gui_utils/config_manager.h
        gui/include/widgets/card_widget.h
        gui/include/widgets/icon_button.h
        gui/include/widgets/search_input.h
        gui/include/widgets/status_tag.h
        gui/include/widgets/status_item_delegate.h
        gui/include/widgets/attendance_list_widget.h
        gui/include/widgets/modern_table_view.h
        gui/include/widgets/title_bar.h
        gui/include/widgets/news_ticker.h
        gui/include/widgets/side_menu.h
        gui/include/widgets/toast_notification.h
        gui/include/ui/recognition_page.h
        gui/include/ui/dashboard_page.h
        gui/include/ui/attendance_page.h
        gui/include/ui/user_management_page.h
        gui/include/ui/settings_page.h
        include/app/local_llm_thread.h
        include/agent/react_agent.h
        include/agent/agent_service.h
        include/agent/agent_worker.h
    )

    add_executable(${TARGET_NAME}_gui
        ${MAIN_GUI_SOURCES}
        ${CORE_SOURCES}
        ${HARDWARE_SOURCES}
        ${DATABASE_SOURCES}
        ${SERVICE_SOURCES}
        ${APP_SOURCES}
        ${GUI_SOURCES}
        ${GUI_SERVICES_SOURCES}
        ${GUI_UTILS_SOURCES}
        ${AGENT_SOURCES}
        ${TOOLS_SOURCES}
    )

    # 保持头文件可见性但不参与编译
    target_sources(${TARGET_NAME}_gui PRIVATE ${GUI_HEADERS})

    # 为 GUI 目标启用 AUTOMOC
    set_target_properties(${TARGET_NAME}_gui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )

    # 设置 AUTOMOC 的 include 目录，让 MOC 能找到 agent 模块的头文件
    set_property(TARGET ${TARGET_NAME}_gui PROPERTY
        AUTOMOC_MOC_OPTIONS "-I${CMAKE_SOURCE_DIR}/include/agent"
    )

    # 添加 target include 目录，确保 moc 文件能被找到
    target_include_directories(${TARGET_NAME}_gui PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_gui_autogen/include
        ${CMAKE_SOURCE_DIR}/include/agent
    )

    target_link_libraries(${TARGET_NAME}_gui
        ${RKNN_RT_LIB}
        ${RKLLM_RT_LIB}
        ${RGA_LIB}
        ${MPP_LIB}
        ${OpenCV_LIBS}
        ${SQLite3_LIBRARIES}
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Svg
        Qt5::Multimedia
        Qt5::Network
        Qt5::Concurrent
        dl
    )
    link_logging_dependencies(${TARGET_NAME}_gui)
    enable_target_warnings(${TARGET_NAME}_gui)

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
link_logging_dependencies(db_tool)
enable_target_warnings(db_tool)

# 仅当 db_tool 不存在时才安装，避免重复编译和替换
install(CODE "
    set(DB_TOOL_SRC \"${CMAKE_BINARY_DIR}/db_tool\")
    set(DB_TOOL_DST \"${CMAKE_INSTALL_PREFIX}/db_tool\")
    if(EXISTS \${DB_TOOL_DST})
        message(STATUS \"db_tool already exists, skipping: \${DB_TOOL_DST}\")
    elseif(EXISTS \${DB_TOOL_SRC})
        message(STATUS \"Installing db_tool: \${DB_TOOL_DST}\")
        file(COPY \${DB_TOOL_SRC} DESTINATION \"${CMAKE_INSTALL_PREFIX}/\")
        file(CHMOD \${DB_TOOL_DST} FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    else()
        message(STATUS \"db_tool was not built, skipping: \${DB_TOOL_DST}\")
    endif()
")

install(PROGRAMS ${RKNN_RT_LIB} DESTINATION lib)
install(PROGRAMS ${RGA_LIB} DESTINATION lib)
install(PROGRAMS ${RKLLM_RT_LIB} DESTINATION lib)
install(FILES
    ${MPP_PATH}/Linux/${LIB_ARCH}/librockchip_mpp.so
    ${MPP_PATH}/Linux/${LIB_ARCH}/librockchip_mpp.so.0
    ${MPP_PATH}/Linux/${LIB_ARCH}/librockchip_mpp.so.1
    DESTINATION lib)


# 安装 data 目录时排除数据库文件，避免覆盖用户运行时的数据
install(DIRECTORY data DESTINATION ./
        PATTERN "*.db" EXCLUDE
        PATTERN "*.db-journal" EXCLUDE
        PATTERN "*.db-wal" EXCLUDE
        PATTERN "*.db-shm" EXCLUDE)

# 仅当源数据库存在时才安装，避免重复生成
# 如果源数据库不存在，用户需要手动运行 scripts/setup_database.sh 初始化
install(CODE "
    set(DB_SRC \"${CMAKE_SOURCE_DIR}/data/database/face_recognition.db\")
    set(DB_DST \"${CMAKE_INSTALL_PREFIX}/data/database/face_recognition.db\")

    if(EXISTS \${DB_SRC})
        if(NOT EXISTS \${DB_DST})
            message(STATUS \"Installing database from source: \${DB_SRC}\")
            file(COPY \${DB_SRC} DESTINATION \"${CMAKE_INSTALL_PREFIX}/data/database/\")
        else()
            message(STATUS \"Database already exists at destination, skipping: \${DB_DST}\")
        endif()
    else()
        message(STATUS \"Source database not found: \${DB_SRC}\")
        message(STATUS \"To initialize database, run: bash scripts/setup_database.sh\")
    endif()
")
