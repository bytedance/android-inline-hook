cmake_minimum_required(VERSION 3.22.1)
project(app)
enable_language(ASM)

find_package(shadowhook REQUIRED CONFIG)

if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(ARCH "arm64")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(ARCH "arm")
endif()

# libhookee.so
set(TARGET "hookee")
file(GLOB SRC hookee/*.c hookee/arch/${ARCH}/*.S)
add_library(${TARGET} SHARED ${SRC})
target_compile_features(${TARGET} PUBLIC c_std_17)
target_compile_options(${TARGET} PUBLIC -std=c17 -Weverything -Werror -Wno-unused-macros)
target_include_directories(${TARGET} PUBLIC hookee)
target_link_libraries(${TARGET} log)

# libhookee2.so
set(TARGET "hookee2")
file(GLOB SRC hookee2/*.c)
add_library(${TARGET} SHARED ${SRC})
target_compile_features(${TARGET} PUBLIC c_std_17)
target_compile_options(${TARGET} PUBLIC -std=c17 -Weverything -Werror)
target_include_directories(${TARGET} PUBLIC hookee2)
target_link_libraries(${TARGET} log)

# libunittest.so
set(TARGET "unittest")
file(GLOB SRC unittest/*.c)
add_library(${TARGET} SHARED ${SRC})
target_compile_features(${TARGET} PUBLIC c_std_17)
target_compile_options(${TARGET} PUBLIC -std=c17 -Weverything -Werror)
target_include_directories(${TARGET} PUBLIC unittest hookee)
target_link_libraries(${TARGET} log hookee shadowhook::shadowhook)
