cmake_minimum_required(VERSION 3.18.1)
project(systest)

find_package(shadowhook REQUIRED CONFIG)

set(TARGET "shadowhooksystest")
file(GLOB SRC *.c)
add_library(${TARGET} SHARED ${SRC})
target_compile_features(${TARGET} PUBLIC c_std_11)
target_compile_options(${TARGET} PUBLIC -Weverything -Werror -Wno-unused-function)
target_include_directories(${TARGET} PUBLIC .)
target_link_libraries(${TARGET} log shadowhook::shadowhook)

if(DEPENDENCY_ON_LOCAL_LIBRARY)
    target_compile_definitions(${TARGET} PUBLIC -DDEPENDENCY_ON_LOCAL_LIBRARY)
endif()

if(USEASAN)
    target_compile_options(${TARGET} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${TARGET} PUBLIC -fsanitize=address)
else()
    target_compile_options(${TARGET} PUBLIC -Oz -flto -ffunction-sections -fdata-sections)
    target_link_options(${TARGET} PUBLIC -Oz -flto -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/systest.map.txt)
endif()
