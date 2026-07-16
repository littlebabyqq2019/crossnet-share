cmake_minimum_required(VERSION 3.15)
project(CrossNetShare VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖库
find_package(Qt5 COMPONENTS Core Widgets Network REQUIRED)

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
)

target_link_libraries(common
    PUBLIC nlohmann_json::nlohmann_json
    PUBLIC Qt5::Core
)

target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 服务器
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
    server/document_converter.cpp
    server/document_converter.h
    server/ui/main_window.cpp
    server/ui/main_window.h
)

target_link_libraries(CrossNetShareServer
    PRIVATE common
    PRIVATE Qt5::Core
    PRIVATE Qt5::Widgets
    PRIVATE Qt5::Network
)

# 客户端
add_executable(CrossNetShareClient
    client/main.cpp
    client/client.cpp
    client/client.h
    client/file_manager.cpp
    client/file_manager.h
    client/ui/main_window.cpp
    client/ui/main_window.h
)

target_link_libraries(CrossNetShareClient
    PRIVATE common
    PRIVATE Qt5::Core
    PRIVATE Qt5::Widgets
    PRIVATE Qt5::Network
)

# Windows特定设置
if(WIN32)
    target_link_libraries(CrossNetShareServer PRIVATE ws2_32)
    target_link_libraries(CrossNetShareClient PRIVATE ws2_32)

    # GUI应用（不显示控制台）
    set_target_properties(CrossNetShareServer PROPERTIES WIN32_EXECUTABLE TRUE)
    set_target_properties(CrossNetShareClient PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
