cmake_minimum_required(VERSION 3.22.1)
project("apexdj_engine")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(oboe REQUIRED CONFIG)

set(ORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch
    ${ORT_DIR}/headers
)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ORT_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so
)

add_library(apexdj_engine SHARED
    FlagshipDspEngine.cpp
)

target_link_libraries(apexdj_engine
    oboe::oboe
    onnxruntime
    android
    mediandk
    log
    m
)
