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
)

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 创建可执行文件
add_executable(subtitle_extractor ${SOURCES})

# 查找 iconv 库 (用于编码转换)
find_library(ICONV_LIBRARY iconv)
if(ICONV_LIBRARY)
    message(STATUS "Found iconv: ${ICONV_LIBRARY}")
    target_link_libraries(subtitle_extractor PRIVATE ${ICONV_LIBRARY})
else()
    # macOS 系统自带 iconv，在 libc 中
    message(STATUS "iconv not found as separate library, using builtin")
endif()

# 平台相关设置
if(WIN32)
    target_link_libraries(subtitle_extractor PRIVATE)
elseif(APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(subtitle_extractor PRIVATE Threads::Threads)
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