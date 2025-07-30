# **shadowhook 手册**

![](https://img.shields.io/badge/license-MIT-brightgreen.svg?style=flat)
![](https://img.shields.io/badge/release-2.0.0-red.svg?style=flat)
![](https://img.shields.io/badge/Android-4.1%20--%2016-blue.svg?style=flat)
![](https://img.shields.io/badge/arch-armeabi--v7a%20%7C%20arm64--v8a-blue.svg?style=flat)

[Readme - English](README.md)


## 介绍

**shadowhook 是一个 Android inline hook 库。** 它的目标是：

- **稳定** - 可以稳定的用于 production app 中。
- **兼容** - 始终保持新版本 API 和 ABI 向后兼容。
- **性能** - 持续降低 API 调用耗时和 hook 后引入的额外运行时耗时。
- **功能** - 除了基本的 hook 功能以外，还提供“hook 引起或相关”问题的通用解决方案。

> 如果你需要的是 Android PLT hook 库，建议试试 [ByteHook](https://github.com/bytedance/bhook)。


## 特征

- 支持 armeabi-v7a 和 arm64-v8a。
- 支持 Android `4.1` - `16`（API level `16` - `36`）。
- 支持 hook 和 intercept。
- 支持通过“地址”或“库名 + 函数名”指定 hook 和 intercept 的目标位置。
- 自动完成对“新加载 ELF”的 hook 和 intercept，执行完成后调用可选的回调函数。
- 自动避免代理函数之间形成的递归环形调用。
- 支持 hook 和 intercept 操作记录，操作记录可随时导出。
- 支持注册 linker 调用新加载 ELF 的 `.init` + `.init_array` 和 `.fini` + `.fini_array` 前后的回调函数。
- 支持绕过 linker namespace 的限制，查询进程中所有 ELF 的 `.dynsym` 和 `.symtab` 中的符号地址。
- 在 hook 代理函数和 intercept 拦截器函数中，兼容 CFI unwind 和 FP unwind。
- 使用 MIT 许可证授权。


## 文档

[shadowhook 手册](doc/manual.zh-CN.md)

> [!CAUTION]
> 下面的「快速开始」能让你的 DEMO 运行起来。但是 shadowhook 并不仅仅是几个 hook API 这么简单，想要在 production app 中稳定的使用 shadowhook，并且充分发挥它的能力，请务必阅读「shadowhook 手册」。


## 快速开始

### 1. 在 build.gradle 中增加依赖

shadowhook 发布在 [Maven Central](https://central.sonatype.com/artifact/com.bytedance.android/shadowhook) 上。为了使用 [native 依赖项](https://developer.android.com/studio/build/native-dependencies)，shadowhook 使用了从 [Android Gradle Plugin 4.0+](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#native-dependencies) 开始支持的 [Prefab](https://google.github.io/prefab/) 包格式。

```Gradle
allprojects {
    repositories {
        mavenCentral()
    }
}
```

```Gradle
android {
    buildFeatures {
        prefab true
    }
}

dependencies {
    implementation 'com.bytedance.android:shadowhook:x.y.z'
}
```

`x.y.z` 请替换成版本号，建议使用最新的 [release](https://github.com/bytedance/android-inline-hook/releases) 版本。

**注意**：shadowhook 使用 [prefab package schema v2](https://github.com/google/prefab/releases/tag/v2.0.0)，它是从 [Android Gradle Plugin 7.1.0](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#7-1-0) 开始作为默认配置的。如果你使用的是 Android Gradle Plugin 7.1.0 之前的版本，请在 `gradle.properties` 中加入以下配置：

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

shadowhook 包含两个 `.so` 文件：libshadowhook.so 和 libshadowhook_nothing.so。

如果你是在一个 SDK 工程里使用 shadowhook，你需要避免把 libshadowhook.so 和 libshadowhook_nothing.so 打包到你的 AAR 里，以免 app 工程打包时遇到重复的 `.so` 文件问题。

```Gradle
android {
    packagingOptions {
        exclude '**/libshadowhook.so'
        exclude '**/libshadowhook_nothing.so'
    }
}
```

另一方面, 如果你是在一个 APP 工程里使用 shadowhook，你可能需要增加一些选项，用来处理重复的 `.so` 文件引起的冲突。**但是，这可能会导致 APP 使用错误版本的 shadowhook。**

```Gradle
android {
    packagingOptions {
        pickFirst '**/libshadowhook.so'
        pickFirst '**/libshadowhook_nothing.so'
    }
}
```

### 5. 初始化

shadowhook 支持三种模式（shared，multi，unique），你可以先试试 unique 模式。

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public void init() {
        ShadowHook.init(new ShadowHook.ConfigBuilder()
            .setMode(ShadowHook.Mode.UNIQUE)
            .build());
    }
}
```

### 6. hook 和 unhook

hook 作用于**函数整体**。你需要编写一个代理函数，这个代理函数需要以和原函数同样的方式接收参数和传递返回值，这通常意味着代理函数需要定义成和原函数同样的类型（包括：参数个数、参数顺序、参数类型、返回值类型）。当 hook 成功后，执行到被 hook 的函数时，会先执行代理函数，在代理函数中你可以自己决定是否调用原函数。

举例：hook libart.so 中的 `art::ArtMethod::Invoke()` 函数。

```C
void *orig = NULL;
void *stub = NULL;

// 被 hook 函数的类型定义
typedef void (*artmethod_invoke_func_type_t)(void *, void *, uint32_t *, uint32_t, void *, const char *);

// 代理函数
void artmethod_invoke_proxy(void *thiz, void *thread, uint32_t *args, uint32_t args_size, void *result, const char *shorty) {
    // do something
    ((artmethod_invoke_func_type_t)orig)(thiz, thread, args, args_size, result, shorty);
    // do something
}

void do_hook() {
    stub = shadowhook_hook_sym_name(
               "libart.so",
               "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc",
               (void *)artmethod_invoke_proxy,
               (void **)&orig);
    
    if(stub == NULL) {
        int err_num = shadowhook_get_errno();
        const char *err_msg = shadowhook_to_errmsg(err_num);
        LOG("hook error %d - %s", err_num, err_msg);
    }
}

void do_unhook() {
    int result = shadowhook_unhook(stub);

    if(result != 0) {
        int err_num = shadowhook_get_errno();
        const char *err_msg = shadowhook_to_errmsg(err_num);
        LOG("unhook error %d - %s", err_num, err_msg);
    }
}
```

- `_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc` 是 `art::ArtMethod::Invoke` 在 libart.so 中经过 C++ Name Mangler 处理后的函数符号名，可以使用 readelf 查看。C 函数没有 Name Mangler 的概念。
- `art::ArtMethod::Invoke` 在 Android M 之前版本中的符号名有所不同，这个例子仅适用于 Android M 及之后的版本。如果要实现更好的 Android 版本兼容性，你需要自己处理函数符号名的差异。

### 7. intercept 和 unintercept

intercept 作用于**指令**。可以是函数的第一条指令，也可以是函数中间的某条指令。你需要编写一个拦截器函数。当 intercept 成功后，执行到被 intercept 的指令时，会先执行拦截器函数。在拦截器函数中，你可以读取和修改寄存器的值。当拦截器函数返回后，会继续执行被 intercept 的指令。intercept 类似于调试器的**断点调试**功能。

举例：intercept libart.so 中的 `art::ArtMethod::Invoke()` 函数中的某条指令。

```C
void *stub;

#if defined(__aarch64__)
void artmethod_invoke_interceptor(shadowhook_cpu_context_t *ctx, void *data) {
    // 当 x19 等于 0 时，修改 x20 和 x21 的值
    if (ctx->regs[19] == 0) {
        ctx->regs[20] = 1;
        ctx->regs[21] = 1000;
        LOG("interceptor: found x19 == 0");
    }

    // 当 q0 等于 0 时，修改 q0，q1，q2，q3 的值
    if (ctx->vregs[0].q == 0) {
        ctx->vregs[0].q = 1;
        ctx->vregs[1].q = 0;
        ctx->vregs[2].q = 0;
        ctx->vregs[3].q = 0;
        LOG("interceptor: found q0 == 0");
    }
}

void do_intercept(void) {
    // 查询 art::ArtMethod::Invoke 的地址
    void *handle = shadowhook_dlopen("libart.so");
    if (handle == NULL) {
        LOG("handle not found");
        return;
    }
    void *sym_addr = shadowhook_dlsym(handle, "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc");
    shadowhook_dlclose(handle);
    if (sym_addr == NULL) {
        LOG("symbol not found");
        return;
    }

    // 定位 art::ArtMethod::Invoke 中的某条指令的地址
    void *instr_addr = (void *)((uintptr_t)sym_addr + 20);
    
    stub = shadowhook_intercept_instr_addr(
               instr_addr,
               artmethod_invoke_interceptor,
               NULL,
               SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_WRITE);

    if(stub == NULL) {
        int err_num = shadowhook_get_errno();
        const char *err_msg = shadowhook_to_errmsg(err_num);
        LOG("intercept failed: %d - %s", err_num, err_msg);
    }
}

void do_unintercept() {
    int result = shadowhook_unintercept(stub);

    if (result != 0) {
        int err_num = shadowhook_get_errno();
        const char *err_msg = shadowhook_to_errmsg(err_num);
        LOG("unintercept failed: %d - %s", err_num, err_msg);
    }
}
#endif
```

- 为了简化示例代码，这里的 `instr_addr` 固定为 `sym_addr + 20`。在真实的场景中，一般会结合内存扫描等手段来确定需要 intercept 的指令的地址。
- 由于 aarch32 和 aarch64 的寄存器不同，同一个函数的指令也不同，所以 intercept 逻辑一般需要分别编写。这里只包含了针对 aarch64 的示例代码。


## 反馈

* [GitHub Issues](https://github.com/bytedance/android-inline-hook/issues)
* [GitHub Discussions](https://github.com/bytedance/android-inline-hook/discussions)


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
Copyright (c) 2020-2025 HexHacking Team
