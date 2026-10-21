cmake_minimum_required(VERSION 3.22.1)
project("apexdj_engine")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(oboe REQUIRED CONFIG)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_library(apexdj_engine SHARED
    FlagshipDspEngine.cpp
)

target_link_libraries(apexdj_engine
    oboe::oboe
    android
    mediandk
    log
    m
)
