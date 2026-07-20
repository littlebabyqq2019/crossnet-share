cmake_minimum_required(VERSION 3.15)
project(CrossNetShare VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖库
if(WIN32)
    find_package(Qt5 COMPONENTS Core Widgets Network AxContainer REQUIRED)
else()
    find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)
endif()

# 第三方库 - nlohmann/json (header-only)
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# 公共库
add_library(common STATIC
    common/protocol.cpp
    common/protocol.h
    common/file_utils.cpp
    common/file_utils.h
    common/message.h
    common/autostart.cpp
    common/autostart.h
)

target_link_libraries(common
    PUBLIC nlohmann_json::nlohmann_json
    PUBLIC Qt5::Core
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 服务器
if(WIN32)
    add_executable(CrossNetShareServer WIN32
        server/main.cpp
        server/server.cpp
        server/server.h
        server/file_indexer.cpp
        server/file_indexer.h
        server/client_handler.cpp
        server/client_handler.h
        server/file_watcher.cpp
        server/file_watcher.h
        server/web_server.cpp
        server/web_server.h
        server/auth_manager.cpp
        server/auth_manager.h
        server/user_manager.cpp
        server/user_manager.h
        server/audit_logger.cpp
        server/audit_logger.h
        server/document_converter.cpp
        server/document_converter.h
        server/watermark_service.cpp
        server/watermark_service.h
        server/ui/main_window.cpp
        server/ui/main_window.h
        server/ui/settings_dialog.cpp
        server/ui/settings_dialog.h
        server/ui/user_management_dialog.cpp
        server/ui/user_management_dialog.h
        server/ui/audit_log_dialog.cpp
        server/ui/audit_log_dialog.h
        server/ui/watermark_settings_dialog.cpp
        server/ui/watermark_settings_dialog.h
        server/server.rc
    )
else()
    add_executable(CrossNetShareServer
        server/main.cpp
        server/server.cpp
        server/server.h
        server/file_indexer.cpp
        server/file_indexer.h
        server/client_handler.cpp
        server/client_handler.h
        server/file_watcher.cpp
        server/file_watcher.h
        server/web_server.cpp
        server/web_server.h
        server/auth_manager.cpp
        server/auth_manager.h
        server/user_manager.cpp
        server/user_manager.h
        server/audit_logger.cpp
        server/audit_logger.h
        server/document_converter.cpp
        server/document_converter.h
        server/watermark_service.cpp
        server/watermark_service.h
        server/ui/main_window.cpp
        server/ui/main_window.h
        server/ui/settings_dialog.cpp
        server/ui/settings_dialog.h
    )
endif()

target_link_libraries(CrossNetShareServer
    PRIVATE common
    PRIVATE Qt5::Core
    PRIVATE Qt5::Widgets
    PRIVATE Qt5::Network
)

# 客户端
if(WIN32)
    add_executable(CrossNetShareClient WIN32
        client/main.cpp
        client/client.cpp
        client/client.h
        client/file_manager.cpp
        client/file_manager.h
        client/ui/main_window.cpp
        client/ui/main_window.h
        client/client.rc
    )
else()
    add_executable(CrossNetShareClient
        client/main.cpp
        client/client.cpp
        client/client.h
        client/file_manager.cpp
        client/file_manager.h
        client/ui/main_window.cpp
        client/ui/main_window.h
    )
endif()

target_link_libraries(CrossNetShareClient
    PRIVATE common
    PRIVATE Qt5::Core
    PRIVATE Qt5::Widgets
    PRIVATE Qt5::Network
)

# Windows特定设置
if(WIN32)
    target_link_libraries(CrossNetShareServer PRIVATE ws2_32 Qt5::AxContainer)
    target_link_libraries(CrossNetShareClient PRIVATE ws2_32)

    # GUI应用（不显示控制台）
    set_target_properties(CrossNetShareServer PROPERTIES WIN32_EXECUTABLE TRUE)
    set_target_properties(CrossNetShareClient PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
