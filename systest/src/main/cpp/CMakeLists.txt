cmake_minimum_required(VERSION 3.18.1)
project(systest)

find_package(shadowhook REQUIRED CONFIG)

file(GLOB SYSTEST_SRC *.c)
add_library(shadowhooksystest SHARED ${SYSTEST_SRC})
target_compile_features(shadowhooksystest PUBLIC c_std_11)
target_compile_options(shadowhooksystest PUBLIC -Weverything -Werror -Wno-unused-function)
target_include_directories(shadowhooksystest PUBLIC .)
target_link_libraries(shadowhooksystest log shadowhook::shadowhook)

if(DEPENDENCY_ON_LOCAL_LIBRARY)
    target_compile_options(shadowhooksystest PUBLIC -DDEPENDENCY_ON_LOCAL_LIBRARY)
endif()

if(USEASAN)
    target_compile_options(shadowhooksystest PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(shadowhooksystest PUBLIC -fsanitize=address)
else()
    target_compile_options(shadowhooksystest PUBLIC -Oz -flto -ffunction-sections -fdata-sections)
    target_link_options(shadowhooksystest PUBLIC -Oz -flto -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/systest.map.txt)
endif()
