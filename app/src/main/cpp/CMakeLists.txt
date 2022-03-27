cmake_minimum_required(VERSION 3.18.1)
project(app)
enable_language(ASM)

find_package(shadowhook REQUIRED CONFIG)

if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(ARCH "arm64")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(ARCH "arm")
endif()

# libhookee.so
file(GLOB HOOKEE_SRC hookee/*.c hookee/arch/${ARCH}/*.S)
add_library(hookee SHARED ${HOOKEE_SRC})
target_compile_features(hookee PUBLIC c_std_11)
target_compile_options(hookee PUBLIC -Weverything -Werror -Wno-unused-macros)
target_include_directories(hookee PUBLIC hookee)
target_link_libraries(hookee log)

# libhookee2.so
file(GLOB HOOKEE2_SRC hookee2/*.c)
add_library(hookee2 SHARED ${HOOKEE2_SRC})
target_compile_features(hookee2 PUBLIC c_std_11)
target_compile_options(hookee2 PUBLIC -Weverything -Werror)
target_include_directories(hookee2 PUBLIC hookee2)
target_link_libraries(hookee2 log)

# libunittest.so
file(GLOB UNITTEST_SRC unittest/*.c)
add_library(unittest SHARED ${UNITTEST_SRC})
target_compile_features(unittest PUBLIC c_std_11)
target_compile_options(unittest PUBLIC -Weverything -Werror)
target_include_directories(unittest PUBLIC unittest hookee)
target_link_libraries(unittest log hookee shadowhook::shadowhook)
