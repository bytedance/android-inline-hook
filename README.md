# android-inline-hook

![](https://img.shields.io/badge/license-MIT-brightgreen.svg?style=flat)
![](https://img.shields.io/badge/release-1.0.2-red.svg?style=flat)
![](https://img.shields.io/badge/Android-4.1%20--%2012-blue.svg?style=flat)
![](https://img.shields.io/badge/arch-armeabi--v7a%20%7C%20arm64--v8a-blue.svg?style=flat)

[README 中文版](README.zh-CN.md)

**shadowhook** is an inline hook library for Android apps.

> shadowhook is a module of "the android-inline-hook project".


## Features

* Support Android 4.1 - 12 (API level 16 - 31).
* Support armeabi-v7a and arm64-v8a.
* Support hook for the whole function, but does not support hook for the middle position of the function.
* Support to specify the hook location by "function address" or "library name + function name".
* Automatically complete the hook of "newly loaded dynamic library" (only "library name + function name"), and call the optional callback function after the hook is completed.
* Multiple hooks and unhooks can be executed concurrently on the same hook point without interfering with each other (only in shared mode).
* Automatically avoid possible recursive calls and circular calls between proxy functions (only in shared mode).
* The proxy function supports unwinding backtrace in a normal way.
* Integrated symbol address search function.
* MIT licensed.


## Documentation

[shadowhook Manual](doc/manual.md)


## Quick Start

You can refer to the sample app in [app module](app), or refer to the hook/unhook examples of commonly used system functions in [systest module](systest).

### 1. Add dependency in build.gradle

shadowhook is published on [Maven Central](https://search.maven.org/), and uses [Prefab](https://google.github.io/prefab/) package format for [native dependencies](https://developer.android.com/studio/build/native-dependencies), which is supported by [Android Gradle Plugin 4.0+](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#native-dependencies).

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
    implementation 'com.bytedance.android:shadowhook:1.0.2'
}
```

### 2. Add dependency in CMakeLists.txt or Android.mk

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

### 3. Specify one or more ABI(s) you need

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

If you are using shadowhook in an SDK project, you may need to avoid packaging lib shadowhook.so into your AAR, so as not to encounter duplicate lib shadowhook.so file when packaging the app project.

```Gradle
android {
    packagingOptions {
        exclude '**/libshadowhook.so'
    }
}
```

On the other hand, if you are using shadowhook in an APP project, you may need to add some options to deal with conflicts caused by duplicate libshadowhook.so file.

```Gradle
android {
    packagingOptions {
        pickFirst '**/libshadowhook.so'
    }
}
```

### 5. Initialize

shadowhook supports two modes (shared mode and unique mode). The proxy function in the two modes is written slightly differently. You can try the unique mode first. 

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        shadowhook.init(new ShadowHook.ConfigBuilder()
            .setMode(ShadowHook.Mode.UNIQUE)
            .build());
    }
}
```

### 6. Hook and Unhook

```C
#include "shadowhook.h"

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

* `shadowhook_hook_sym_addr`: hook a function address.
* `shadowhook_hook_sym_name`: hook the symbol name of a function in a dynamic library.
* `shadowhook_hook_sym_name_callback`: Similar to `shadowhook_hook_sym_name`, but the specified callback function will be called after the hook is completed.
* `shadowhook_unhook`: unhook.

For example, let's try to hook `art::ArtMethod::Invoke`:

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

* `_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc` is the function symbol name of `art::ArtMethod::Invoke` processed by C++ Name Mangler in libart.so. You can use readelf to view it. The C function does not have the concept of Name Mangler.
* The symbol name of `art::ArtMethod::Invoke` is different in previous versions of Android M. This example is only applicable to Android M and later versions. If you want to achieve better Android version compatibility, you need to handle the difference in function symbol names yourself.


## Contributing

[Contributing Guide](CONTRIBUTING.md)


## License

shadowhook is licensed by [MIT License](LICENSE).

shadowhook uses the following third-party source code or libraries:

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
