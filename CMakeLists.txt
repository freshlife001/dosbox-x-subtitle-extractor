cmake_minimum_required(VERSION 3.10)
project(dosbox-x-subtitle-extractor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项：是否启用 PaddleOCR（默认关闭）
option(USE_PADDLEOCR "Enable PaddleOCR support (requires paddle_inference library)" OFF)

# 源文件（基础）
set(SOURCES
    src/gamelink_interface.cpp
    src/subtitle_extractor.cpp
    src/subtitle_format.cpp
    src/main.cpp
    src/web_server.cpp
    src/ocr_bridge.cpp
)

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 平台特定 OCR 源文件
if(APPLE)
    # macOS: 支持 Vision OCR 和 Ollama OCR (Objective-C++)
    list(APPEND SOURCES src/vision_ocr.mm)
    list(APPEND SOURCES src/ollama_ocr.mm)
    message(STATUS "Platform: macOS - Vision OCR + Ollama OCR supported")
elseif(WIN32)
    # Windows: 仅支持 Ollama OCR (纯 C++ HTTP 实现)
    list(APPEND SOURCES src/ollama_ocr.cpp)
    message(STATUS "Platform: Windows - Ollama OCR only (Vision OCR not available)")
else()
    # Linux: 仅支持 Ollama OCR
    list(APPEND SOURCES src/ollama_ocr.cpp)
    message(STATUS "Platform: Linux - Ollama OCR only")
endif()

# 如果启用 PaddleOCR
if(USE_PADDLEOCR)
    # Paddle Inference library path
    set(PADDLE_INFER_DIR ${CMAKE_SOURCE_DIR}/paddle_inference/paddle_inference_install_dir)
    include_directories(${PADDLE_INFER_DIR}/paddle/include)

    # 添加 PaddleOCR 源文件
    list(APPEND SOURCES src/paddle_ocr.cpp)

    message(STATUS "PaddleOCR enabled - using paddle_inference from: ${PADDLE_INFER_DIR}")
else()
    message(STATUS "PaddleOCR disabled")
endif()

# 复制资源文件到构建目录
file(COPY ${CMAKE_SOURCE_DIR}/resources DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/models DESTINATION ${CMAKE_BINARY_DIR})

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

# libwebsockets
find_library(LWS_LIBRARY websockets
    PATHS /opt/homebrew/lib /usr/local/lib)
find_path(LWS_INCLUDE_DIR "libwebsockets.h"
    PATHS /opt/homebrew/include /usr/local/include)
if(LWS_LIBRARY AND LWS_INCLUDE_DIR)
    message(STATUS "Found libwebsockets: ${LWS_LIBRARY}")
    include_directories(${LWS_INCLUDE_DIR})
    target_link_libraries(subtitle_extractor PRIVATE ${LWS_LIBRARY})
else()
    message(FATAL_ERROR "libwebsockets not found - install with: brew install libwebsockets")
endif()

# macOS 特定框架
if(APPLE)
    find_package(Threads REQUIRED)
    find_library(VISION_FRAMEWORK Vision REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(IMAGEIO_FRAMEWORK ImageIO REQUIRED)

    target_link_libraries(subtitle_extractor PRIVATE
        Threads::Threads
        ${VISION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK}
        ${OpenCV_LIBS}
    )

    # 如果启用 PaddleOCR，链接相关库
    if(USE_PADDLEOCR)
        set(PADDLE_LIB_DIR ${PADDLE_INFER_DIR}/paddle/lib)
        target_link_libraries(subtitle_extractor PRIVATE
            ${PADDLE_LIB_DIR}/libpaddle_inference.dylib
            ${PADDLE_LIB_DIR}/libcommon.dylib
            ${PADDLE_LIB_DIR}/libphi.dylib
            ${PADDLE_LIB_DIR}/libphi_core.dylib
        )

        # Set rpath for paddle libraries
        set_target_properties(subtitle_extractor PROPERTIES
            INSTALL_RPATH ${PADDLE_LIB_DIR}
            BUILD_RPATH ${PADDLE_LIB_DIR}
        )
    endif()
elseif(WIN32)
    # Windows: 使用 Winsock for HTTP
    target_link_libraries(subtitle_extractor PRIVATE
        ws2_32
        ${OpenCV_LIBS}
    )
else()  # Linux
    find_package(Threads REQUIRED)
    target_link_libraries(subtitle_extractor PRIVATE Threads::Threads rt ${OpenCV_LIBS})
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