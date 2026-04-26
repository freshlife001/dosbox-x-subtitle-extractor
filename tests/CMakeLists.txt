cmake_minimum_required(VERSION 3.10)
project(test_ocr)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/../include)

# OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# Source files
set(TEST_SRC test_ocr.cpp)

# OCR sources - platform specific
if(APPLE)
    find_library(VISION_FRAMEWORK Vision REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    find_library(IMAGEIO_FRAMEWORK ImageIO REQUIRED)

    set(OCR_SRC
        ${CMAKE_SOURCE_DIR}/../src/vision_ocr.mm
        ${CMAKE_SOURCE_DIR}/../src/ollama_ocr.mm
        ${CMAKE_SOURCE_DIR}/../src/ocr_bridge.cpp
    )

    add_executable(test_ocr ${TEST_SRC} ${OCR_SRC})

    target_link_libraries(test_ocr PRIVATE
        ${OpenCV_LIBS}
        opencv_freetype
        ${VISION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${COREFOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK}
    )
elseif(WIN32)
    set(OCR_SRC
        ${CMAKE_SOURCE_DIR}/../src/ollama_ocr.cpp
        ${CMAKE_SOURCE_DIR}/../src/ocr_bridge.cpp
    )

    add_executable(test_ocr ${TEST_SRC} ${OCR_SRC})

    target_link_libraries(test_ocr PRIVATE
        ${OpenCV_LIBS}
        ws2_32
    )
else()
    # Linux
    set(OCR_SRC
        ${CMAKE_SOURCE_DIR}/../src/ollama_ocr.cpp
        ${CMAKE_SOURCE_DIR}/../src/ocr_bridge.cpp
    )

    add_executable(test_ocr ${TEST_SRC} ${OCR_SRC})

    find_package(Threads REQUIRED)
    target_link_libraries(test_ocr PRIVATE
        ${OpenCV_LIBS}
        Threads::Threads
    )
endif()

# Copy resources to build directory
file(COPY ${CMAKE_SOURCE_DIR}/../resources DESTINATION ${CMAKE_BINARY_DIR})