# ShadowHook

![](https://img.shields.io/badge/license-MIT-brightgreen.svg?style=flat)
![](https://img.shields.io/badge/release-1.0.6-red.svg?style=flat)
![](https://img.shields.io/badge/Android-4.1%20--%2013-blue.svg?style=flat)
![](https://img.shields.io/badge/arch-armeabi--v7a%20%7C%20arm64--v8a-blue.svg?style=flat)

[README English Version](README.md)

**ShadowHook** 是一个 Android inline hook 库，它支持 thumb、arm32 和 arm64。

ShadowHook 现在被用于 TikTok，抖音，今日头条，西瓜视频，飞书中。

如果你需要的是 Android PLT hook 库，请移步到 [ByteHook](https://github.com/bytedance/bhook)。


## 特征

* 支持 Android 4.1 - 13 (API level 16 - 33)。
* 支持 armeabi-v7a 和 arm64-v8a。
* 支持针对函数整体的 hook，不支持对函数中间位置的 hook。
* 支持通过“函数地址”或“库名 + 函数名”的方式指定 hook 位置。
* 自动完成“新加载动态库”的 hook（仅限“库名 + 函数名”方式），hook 完成后调用可选的回调函数。
* 可对同一个 hook 点并发执行多个 hook 和 unhook，彼此互不干扰（仅限 shared 模式）。
* 自动避免代理函数之间可能形成的递归调用和环形调用（仅限 shared 模式）。
* 代理函数中支持以正常的方式（CFI，EH，FP）回溯调用栈。
* 集成符号地址查找功能。
* 使用 MIT 许可证授权。


## 文档

[ShadowHook 手册](doc/manual.zh-CN.md)


## 快速开始

你可以参考 [app module](app) 中的示例 app，也可以参考  [systest module](systest) 中对常用系统函数的 hook / unhook 示例。

### 1. 在 build.gradle 中增加依赖

ShadowHook 发布在 [Maven Central](https://search.maven.org/) 上。为了使用 [native 依赖项](https://developer.android.com/studio/build/native-dependencies)，ShadowHook 使用了从 [Android Gradle Plugin 4.0+](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#native-dependencies) 开始支持的 [Prefab](https://google.github.io/prefab/) 包格式。

```Gradle
android {
    buildFeatures {
        prefab true
    }
}

dependencies {
    implementation 'com.bytedance.android:shadowhook:1.0.6'
}
```

**注意**：ShadowHook 使用 [prefab package schema v2](https://github.com/google/prefab/releases/tag/v2.0.0)，它是从 [Android Gradle Plugin 7.1.0](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#7-1-0) 开始作为默认配置的。如果你使用的是 Android Gradle Plugin 7.1.0 之前的版本，请在 `gradle.properties` 中加入以下配置：

```
android.prefabVersion=2.0.0
```

### 2. 在 CMakeLists.txt 或 Android.mk 中增加依赖

> CMakeLists.txt

```CMake
find_package(shadowhook REQUIRED CONFIG)

add_library(mylib SHARED mylib.c)
target_link_libraries(mylib shadowhook::shadowhook)
```

> Android.mk

```
include $(CLEAR_VARS)
LOCAL_MODULE           := mylib
LOCAL_SRC_FILES        := mylib.c
LOCAL_SHARED_LIBRARIES += shadowhook
include $(BUILD_SHARED_LIBRARY)

$(call import-module,prefab/shadowhook)
```

### 3. 指定一个或多个你需要的 ABI

```Gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a'
        }
    }
}
```

### 4. 增加打包选项

如果你是在一个 SDK 工程里使用 ShadowHook，你可能需要避免把 libshadowhook.so 打包到你的 AAR 里，以免 app 工程打包时遇到重复的 libshadowhook.so 文件。

```Gradle
android {
    packagingOptions {
        exclude '**/libshadowhook.so'
    }
}
```

另一方面, 如果你是在一个 APP 工程里使用 ShadowHook，你可以需要增加一些选项，用来处理重复的 libshadowhook.so 文件引起的冲突。

```Gradle
android {
    packagingOptions {
        pickFirst '**/libshadowhook.so'
    }
}
```

### 5. 初始化

ShadowHook 支持两种模式（shared 模式和 unique 模式），两种模式下的 proxy 函数写法稍有不同，你可以先尝试一下 unique 模式。

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init(new ShadowHook.ConfigBuilder()
            .setMode(ShadowHook.Mode.UNIQUE)
            .build());
    }
}
```

### 6. Hook 和 Unhook

```C
#include "shadowhook.h"

void *shadowhook_hook_func_addr(
    void *func_addr,
    void *new_addr,
    void **orig_addr);

void *shadowhook_hook_sym_addr(
    void *sym_addr,
    void *new_addr,
    void **orig_addr);

void *shadowhook_hook_sym_name(
    const char *lib_name,
    const char *sym_name,
    void *new_addr,
    void **orig_addr);

typedef void (*shadowhook_hooked_t)(
    int error_number,
    const char *lib_name,
    const char *sym_name,
    void *sym_addr,
    void *new_addr,
    void *orig_addr,
    void *arg);

void *shadowhook_hook_sym_name_callback(
    const char *lib_name,
    const char *sym_name,
    void *new_addr,
    void **orig_addr,
    shadowhook_hooked_t hooked,
    void *hooked_arg);

int shadowhook_unhook(void *stub);
```

* `shadowhook_hook_func_addr`: 通过绝对地址 hook 一个在 ELF 中没有符号信息的函数。
* `shadowhook_hook_sym_addr`：通过绝对地址 hook 一个在 ELF 中有符号信息的函数。
* `shadowhook_hook_sym_name`：通过符号名和 ELF 的文件名或路径名 hook 一个函数。
* `shadowhook_hook_sym_name_callback`：和 `shadowhook_hook_sym_name` 类似，但是会在 hook 完成后调用指定的回调函数。
* `shadowhook_unhook`：unhook。

举个例子，我们尝试 hook 一下 `art::ArtMethod::Invoke`：

```C
void *orig = NULL;
void *stub = NULL;

typedef void (*type_t)(void *, void *, uint32_t *, uint32_t, void *, const char *);

void proxy(void *thiz, void *thread, uint32_t *args, uint32_t args_size, void *result, const char *shorty)
{
    // do something
    ((type_t)orig)(thiz, thread, args, args_size, result, shorty);
    // do something
}

void do_hook()
{
    stub = shadowhook_hook_sym_name(
               "libart.so",
               "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc",
               (void *)proxy,
               (void **)&orig);
    
    if(stub == NULL)
    {
        int err_num = shadowhook_get_errno();
        const char *err_msg = shadowhook_to_errmsg(err_num);
        LOG("hook error %d - %s", err_num, err_msg);
    }
}

void do_unhook()
{
    shadowhook_unhook(stub);
    stub = NULL;
}
```

* `_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc` 是 `art::ArtMethod::Invoke` 在 libart.so 中经过 C++ Name Mangler 处理后的函数符号名，可以使用 readelf 查看。C 函数没有 Name Mangler 的概念。
* `art::ArtMethod::Invoke` 在 Android M 之前版本中的符号名有所不同，这个例子仅适用于 Android M 及之后的版本。如果要实现更好的 Android 版本兼容性，你需要自己处理函数符号名的差异。


## 贡献

* [Code of Conduct](CODE_OF_CONDUCT.md)
* [Contributing Guide](CONTRIBUTING.md)
* [Reporting Security vulnerabilities](SECURITY.md)


## 许可证

ShadowHook 使用 [MIT 许可证](LICENSE) 授权。

ShadowHook 使用了以下第三方源码或库：

* [queue.h](shadowhook/src/main/cpp/third_party/bsd/queue.h)  
BSD 3-Clause License  
Copyright (c) 1991, 1993 The Regents of the University of California.
* [tree.h](shadowhook/src/main/cpp/third_party/bsd/tree.h)  
BSD 2-Clause License  
Copyright (c) 2002 Niels Provos <provos@citi.umich.edu>
* [linux-syscall-support](https://chromium.googlesource.com/linux-syscall-support/)  
BSD 3-Clause License  
Copyright (c) 2005-2011 Google Inc.
* [xDL](https://github.com/hexhacking/xDL)  
MIT License  
Copyright (c) 2020-2021 HexHacking Team
