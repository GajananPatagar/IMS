cmake_minimum_required(VERSION 3.22.1)
project("ims_neuralink_5b")

add_library(
    ims_neuralink_5b
    SHARED
    neuralink_5b_brain.cpp
)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(
    ims_neuralink_5b
    ${log-lib}
    ${android-lib}
)
