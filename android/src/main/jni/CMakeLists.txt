cmake_minimum_required(VERSION 3.13)
project(webworker)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find React Native prefab packages
find_package(ReactAndroid REQUIRED CONFIG)
find_package(fbjni REQUIRED CONFIG)
find_package(hermes-engine REQUIRED CONFIG)

# Shared C++ source files from cpp/ directory
set(SHARED_CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp")

set(SHARED_SOURCES
    ${SHARED_CPP_DIR}/WebWorkerCore.cpp
    ${SHARED_CPP_DIR}/TaskQueue.cpp
)

# Platform-specific JNI wrapper
set(ANDROID_SOURCES
    WebWorkerJNI.cpp
)

# Create the native library
add_library(
    webworker
    SHARED
    ${SHARED_SOURCES}
    ${ANDROID_SOURCES}
)

# Include directories
target_include_directories(
    webworker
    PRIVATE
    ${SHARED_CPP_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against React Native libraries
# ReactAndroid::jsi - JavaScript Interface
# ReactAndroid::reactnative - React Native core (includes JSI runtime support)
# hermes-engine::hermesvm - Hermes VM runtime for creating worker runtimes
# fbjni::fbjni - Facebook JNI bindings
target_link_libraries(
    webworker
    ReactAndroid::jsi
    ReactAndroid::reactnative
    hermes-engine::hermesvm
    fbjni::fbjni
    android
    log
)
