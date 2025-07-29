# **shadowhook Manual**

![](https://img.shields.io/badge/license-MIT-brightgreen.svg?style=flat)
![](https://img.shields.io/badge/release-2.0.0-red.svg?style=flat)
![](https://img.shields.io/badge/Android-4.1%20--%2016-blue.svg?style=flat)
![](https://img.shields.io/badge/arch-armeabi--v7a%20%7C%20arm64--v8a-blue.svg?style=flat)

[Readme - 简体中文](README.zh-CN.md)


## Introduction

**shadowhook is an Android inline hook library.** Its goals are:

- **Stability** - Can be stably used in production apps.
- **Compatibility** - Always maintains backward compatibility of API and ABI in new versions.
- **Performance** - Continuously reduces API call overhead and additional runtime overhead introduced by hooks.
- **Functionality** - Besides basic hook functionality, provides general solutions for "hook-related" issues.

> If you need an Android PLT hook library, try [ByteHook](https://github.com/bytedance/bhook).


## Features

- Supports armeabi-v7a and arm64-v8a.
- Supports Android `4.1` - `16` (API level `16` - `36`).
- Supports hook and intercept.
- Supports specifying hook and intercept target locations via "address" or "library name + function name".
- Automatically completes hook and intercept for "newly loaded ELFs", with optional callbacks after execution.
- Automatically prevents recursive circular calls between proxy functions.
- Supports hook and intercept operation recording, which can be exported at any time.
- Supports registering callbacks before and after linker calls `.init` + `.init_array` and `.fini` + `.fini_array` of newly loaded ELFs.
- Supports bypassing linker namespace restrictions to query symbol addresses in `.dynsym` and `.symtab` of all ELFs in the process.
- Compatible with CFI unwind and FP unwind in hook proxy functions and intercept interceptor functions.
- Licensed under the MIT license.


## Documentation

[shadowhook Manual](doc/manual.md)

> [!CAUTION]
> The "Quick Start" below will get your DEMO running. However, shadowhook is not just a few hook APIs. To use shadowhook stably in production apps and fully leverage its capabilities, please be sure to read the "shadowhook Manual".


## Quick Start

### 1. Add dependencies in build.gradle

shadowhook is published on [Maven Central](https://search.maven.org/). To use [native dependencies](https://developer.android.com/studio/build/native-dependencies), shadowhook uses the [Prefab](https://google.github.io/prefab/) package format, which is supported from [Android Gradle Plugin 4.0+](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#native-dependencies).

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

Replace `x.y.z` with the version number. It's recommended to use the latest [release](https://github.com/bytedance/android-inline-hook/releases) version.

**Note**: shadowhook uses [prefab package schema v2](https://github.com/google/prefab/releases/tag/v2.0.0), which is the default configuration from [Android Gradle Plugin 7.1.0](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#7-1-0). If you're using a version of Android Gradle Plugin prior to 7.1.0, add the following configuration to `gradle.properties`:

```
android.prefabVersion=2.0.0
```

### 2. Add dependencies in CMakeLists.txt or Android.mk

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

### 3. Specify one or more ABIs you need

```Gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a'
        }
    }
}
```

### 4. Add packaging options

shadowhook includes two `.so` files: libshadowhook.so and libshadowhook_nothing.so.

If you're using shadowhook in an SDK project, you need to avoid packaging libshadowhook.so and libshadowhook_nothing.so into your AAR to prevent duplicate `.so` file issues when packaging the app.

```Gradle
android {
    packagingOptions {
        exclude '**/libshadowhook.so'
        exclude '**/libshadowhook_nothing.so'
    }
}
```

On the other hand, if you're using shadowhook in an APP project, you may need to add some options to handle conflicts caused by duplicate `.so` files. **However, this may cause the APP to use an incorrect version of shadowhook.**

```Gradle
android {
    packagingOptions {
        pickFirst '**/libshadowhook.so'
        pickFirst '**/libshadowhook_nothing.so'
    }
}
```

### 5. Initialize

shadowhook supports three modes (shared, multi, unique). You can try the unique mode first.

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

### 6. hook and unhook

Hook applies to the **entire function**. You need to write a proxy function that receives arguments and passes return values in the same way as the original function, which typically means the proxy function needs to be defined with the same type as the original function (including: number of parameters, parameter order, parameter types, return value type). When the hook is successful, when the hooked function is executed, the proxy function will be executed first, and in the proxy function, you can decide whether to call the original function.

Example: hook the `art::ArtMethod::Invoke()` function in libart.so.

```C
void *orig = NULL;
void *stub = NULL;

// Type definition of the hooked function
typedef void (*artmethod_invoke_func_type_t)(void *, void *, uint32_t *, uint32_t, void *, const char *);

// Proxy function
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

- `_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc` is the function symbol name of `art::ArtMethod::Invoke` in libart.so after C++ Name Mangler processing, which can be viewed using readelf. C functions do not have the concept of Name Mangler.
- The symbol name of `art::ArtMethod::Invoke` is different in versions before Android M. This example only applies to Android M and later versions. If you want to achieve better Android version compatibility, you need to handle the differences in function symbol names yourself.

### 7. intercept and unintercept

Intercept applies to **instructions**. It can be the first instruction of a function or a certain instruction in the middle of a function. You need to write an interceptor function. When the intercept is successful, when the intercepted instruction is executed, the interceptor function will be executed first. In the interceptor function, you can read and modify the values of registers. After the interceptor function returns, the intercepted instruction will continue to be executed. Intercept is similar to the **breakpoint debugging** function of a debugger.

Example: intercept a certain instruction in the `art::ArtMethod::Invoke()` function in libart.so.

```C
void *stub;

#if defined(__aarch64__)
void artmethod_invoke_interceptor(shadowhook_cpu_context_t *ctx, void *data) {
    // When x19 equals 0, modify the values of x20 and x21
    if (ctx->regs[19] == 0) {
        ctx->regs[20] = 1;
        ctx->regs[21] = 1000;
        LOG("interceptor: found x19 == 0");
    }

    // When q0 equals 0, modify the values of q0, q1, q2, q3
    if (ctx->vregs[0].q == 0) {
        ctx->vregs[0].q = 1;
        ctx->vregs[1].q = 0;
        ctx->vregs[2].q = 0;
        ctx->vregs[3].q = 0;
        LOG("interceptor: found q0 == 0");
    }
}

void do_intercept(void) {
    // Query the address of art::ArtMethod::Invoke
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

    // Locate the address of a certain instruction in art::ArtMethod::Invoke
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

- To simplify the example code, `instr_addr` is fixed as `sym_addr + 20`. In real scenarios, memory scanning and other methods are generally used to determine the address of the instruction that needs to be intercepted.
- Since aarch32 and aarch64 have different registers and the instructions of the same function are different, the intercept logic generally needs to be written separately. Here only includes example code for aarch64.


## Contributing

* [Code of Conduct](CODE_OF_CONDUCT.md)
* [Contributing Guide](CONTRIBUTING.md)
* [Reporting Security vulnerabilities](SECURITY.md)


## License

ShadowHook is licensed under the [MIT License](LICENSE).

ShadowHook uses the following third-party source code or libraries:

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
