cmake_minimum_required(VERSION 3.10)
project(dosbox-x-subtitle-extractor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    src/gamelink_interface.cpp
    src/subtitle_extractor.cpp
    src/subtitle_format.cpp
    src/main.cpp
    src/ocr_bridge.mm
    src/web_server.cpp
)

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 创建可执行文件
add_executable(subtitle_extractor ${SOURCES})

# 查找 iconv 库
find_library(ICONV_LIBRARY iconv)
if(ICONV_LIBRARY)
    message(STATUS "Found iconv: ${ICONV_LIBRARY}")
    target_link_libraries(subtitle_extractor PRIVATE ${ICONV_LIBRARY})
else()
    message(STATUS "iconv not found as separate library, using builtin")
endif()

# macOS 特定框架
if(APPLE)
    find_package(Threads REQUIRED)
    find_library(VISION_FRAMEWORK Vision REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(subtitle_extractor PRIVATE
        Threads::Threads
        ${VISION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
    )
elseif(WIN32)
    target_link_libraries(subtitle_extractor PRIVATE ws2_32)
else()  # Linux
    find_package(Threads REQUIRED)
    target_link_libraries(subtitle_extractor PRIVATE Threads::Threads rt)
endif()

# 编译器选项
if(MSVC)
    target_compile_options(subtitle_extractor PRIVATE /W4 /permissive-)
else()
    target_compile_options(subtitle_extractor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 安装
install(TARGETS subtitle_extractor DESTINATION bin)
install(DIRECTORY game_profiles DESTINATION share/subtitle-extractor)