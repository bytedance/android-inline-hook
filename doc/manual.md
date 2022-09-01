# **ShadowHook Manual**

[ShadowHook 手册 - 中文版](manual.zh-CN.md)


# Introduction

**ShadowHook** is an Android inline hook library which supports thumb, arm32 and arm64.

If you need an Android PLT hook library, please move to [ByteHook](https://github.com/bytedance/bhook).


# Features

* Support Android 4.1 - 13 (API level 16 - 33).
* Support armeabi-v7a and arm64-v8a.
* Support hook for the whole function, but does not support hook for the middle position of the function.
* Support to specify the hook location by "function address" or "library name + function name".
* Automatically complete the hook of "newly loaded dynamic library" (only "library name + function name"), and call the optional callback function after the hook is completed.
* Multiple hooks and unhooks can be executed concurrently on the same hook point without interfering with each other (only in shared mode).
* Automatically avoid possible recursive calls and circular calls between proxy functions (only in shared mode).
* The proxy function supports unwinding backtrace in a normal way (CFI, EH, FP).
* Integrated symbol address search function.
* MIT licensed.


# Integration

## Add dependency in build.gradle

ShadowHook is published on [Maven Central](https://search.maven.org/), and uses [Prefab](https://google.github.io/prefab/) package format for [native dependencies](https://developer.android.com/studio/build/native-dependencies), which is supported by [Android Gradle Plugin 4.0+](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#native-dependencies).

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

Please replace `x.y.z` with the version number. It is recommended to use the latest [release](https://github.com/bytedance/android-inline-hook/releases) version.

**Note**: ShadowHook uses the [prefab package schema v2](https://github.com/google/prefab/releases/tag/v2.0.0), which is configured by default since [Android Gradle Plugin 7.1.0](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#7-1-0). If you are using Android Gradle Plugin earlier than 7.1.0, please add the following configuration to `gradle.properties`:

```
android.prefabVersion=2.0.0
```

## Add dependency in CMakeLists.txt or Android.mk

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

## Specify one or more ABI(s) you need

```Gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a'
        }
    }
}
```

## Add packaging options

If you are using ShadowHook in an SDK project, you may need to avoid packaging libshadowhook.so into your AAR, so as not to encounter duplicate libshadowhook.so file when packaging the app project.

```Gradle
android {
    packagingOptions {
        exclude '**/libshadowhook.so'
    }
}
```

On the other hand, if you are using ShadowHook in an APP project, you may need to add some options to deal with conflicts caused by duplicate libshadowhook.so file.

```Gradle
android {
    packagingOptions {
        pickFirst '**/libshadowhook.so'
    }
}
```


# Initialize

* **ShadowHook can be initialized at the java layer or the native layer, and you can choose one of the two.**
* The java layer initialization logic actually only does two things: `System.loadLibrary`; calling the `init` function of the native layer.
* The initialization can be executed multiple times concurrently, but only the first time actually takes effect, and subsequent initialization calls will directly return the return value of the first initialization.

## Initialization Parameters

### Mode

* `shared` mode (default): Multiple hooks and unhooks can be executed concurrently on the same hook point without interfering with each other. Automatically avoid recursive and circular calls that may form between proxy functions. The shared mode is recommended for complex institutions or organizations.
* `unique` mode: The same hook point can only be hooked once (and can be hooked again after unhook). You need to deal with recursive calls and circular calls that may be formed between proxy functions. This mode can be used for personal or small apps, or in some debugging scenarios (for example, if you want to skip the proxy management mechanism of ShadowHook, and debug and analyze the simple inlinehook process).

### Debug Log

* `true`: On. Debug information will be written to logcat. tag: `shadowhook_tag`.
* `false` (default): Off.

## Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static int init()
public static int init(Config config)
```

Returns `0` for success, non-`0` for failure (non-`0` is an error code).

### Example (using default initialization parameters)

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init();
    }
}
```

### Example (specify more options through the Config parameter)

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init(new ShadowHook.ConfigBuilder()
            .setMode(ShadowHook.Mode.SHARED)
            .setDebuggable(true)
            .build());
    }
}
```

## Native API

```C
#include "shadowhook.h"

typedef enum
{
    SHADOWHOOK_MODE_SHARED = 0,
    SHADOWHOOK_MODE_UNIQUE = 1
} shadowhook_mode_t;

int shadowhook_init(shadowhook_mode_t mode, bool debuggable);
```

Returns `0` for success, non-`0` for failure (non-`0` is an error code).


# Find symbol address

* The hook API of ShadowHook supports specifying the hook location by "function address" or "library name + function name".
* The function addresses in `.dynsym` exposed by NDK can be obtained through linker, but the function addresses in `.symtab` and `.symtab in .gnu_debugdata` need to be obtained by other means.

```C
#include "shadowhook.h"

void *shadowhook_dlopen(const char *lib_name);
void shadowhook_dlclose(void *handle);
void *shadowhook_dlsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_dynsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_symtab(void *handle, const char *sym_name);
```

* The usage of this set of APIs is similar to the system-provided `dlopen`, `dlclose`, `dlsym`.
* `shadowhook_dlsym_dynsym` can only find symbols in `.dynsym`, which is faster.
* `shadowhook_dlsym_symtab` can find symbols in `.symtab` and `.symtab in .gnu_debugdata`, but is slower.
* `shadowhook_dlsym` will first try to find the symbol in `.dynsym`, and if not found, it will continue to try to find it in `.symtab` and `.symtab in .gnu_debugdata`.


# hook and unhook

* Supports specifying the hook location by "function address" or "library name + function name".
* Automatically completes the hook of "newly loaded so library" (only "library name + function name" method), and calls an optional callback function after the hook is completed.
* Only hooks for the whole function are supported, and hooks for the middle of the function are not supported.
* If the function to be hooked is not in the ELF's symbol table (`.dynsym` or `.symtab` or `.symtab in .gnu_debugdata`), the hook will definitely not succeed. Because currently ShadowHook determines the length of the function by parsing the ELF symbol table, preventing the problem of "because the length of the function is too short, the hook operation covers the instructions of other subsequent functions". The design thinking on this problem is: the goal of ShadowHook is not for offline reverse analysis, but for online. To stably find the function location that needs hook online, the most reliable way is to locate the function through symbol starting point. In addition, based on this consideration, ShadowHook abandons "the ability of  hooking any position in a function", and only supports "hooking the head of a function (that is, hooking the function as a whole)".

## 1. Execute hook by "function address"

```C
#include "shadowhook.h"

void *shadowhook_hook_sym_addr(void *sym_addr, void *new_addr, void **orig_addr);
```

This method can only hook "dynamic libraries that are currently loaded into the process".

### Parameters

* `sym_addr` (must be specified): The absolute address of the function that needs to be hooked.
* `new_addr` (must be specified): The absolute address of the new function (proxy function).
* `orig_addr` (you can pass `NULL` if not needed): Return the address of the original function.

### Return Value

* Not `NULL`: the hook succeeded. The return value is a stub, you can save the return value for subsequent use in unhook.
* `NULL`: The hook failed. You can call `shadowhook_get_errno` to get the errno, and you can continue to call `shadowhook_to_errmsg` to get the error message.

### Example

```C
void *orig;
void *stub = shadowhook_hook_sym_addr(malloc, my_malloc, &orig);
if(stub == NULL)
{
    int error_num = shadowhook_get_errno();
    const char *error_msg = shadowhook_to_errmsg(error_num);
    __android_log_print(ANDROID_LOG_WARN,  "test", "hook failed: %d - %s", error_num, error_msg);
}
```

In this example, `sym_addr` is specified by the linker when loading the current dynamic library.

## 2. Execute hook by "library name + function name"

```C
#include "shadowhook.h"

void *shadowhook_hook_sym_name(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr);
```

* In this way, you can hook "the dynamic library that is currently loaded into the process", or you can hook "the dynamic library that has not been loaded into the process" (if the dynamic library has not been loaded at the time of hooking, ShadowHook will internally record the current hook "demand", and the hook operation will be executed immediately once the target dynamic library is loaded into memory).
* ShadowHook can hook symbols in `.dynsym` / `.symtab` / `.symtab in .gnu_debugdata` in ELF, and `shadowhook_hook_sym_name` will complete the work of symbol address lookup.
* Symbols in `.dynsym` / `.symtab` can be viewed with `readelf`.
* Symbols in `.symtab in .gnu_debugdata` can be viewed with `dd` + `xz` + `readelf`.
* If you need to hook "multiple symbols from the same dynamic library", it is recommended to use the `shadowhook_dl*` API to complete the symbol address lookup in batches, which can speed up the symbol lookup in the hook process.

### Parameters

* `lib_name` (must be specified): The basename or pathname of the ELF where the symbol is located. For the only dynamic library identified in the process, you can only pass the basename, for example: `libart.so`. For non-unique dynamic libraries, you need to handle compatibility according to Android version and arch, for example: `/system/lib64/libbinderthreadstate.so` and `/system/lib64/vndk-sp-29/libbinderthreadstate.so`. Otherwise, ShadowHook will only hook the first dynamic library that matches basename in the process.
* `sym_name` (must be specified): Symbol name.
* `new_addr` (must be specified): The absolute address of the new function (proxy function).
* `orig_addr` (you can pass `NULL` if not needed): Return the address of the original function.

### Return Value

* Not `NULL` (errno == 0): the hook succeeded. The return value is a stub, you can save the return value for subsequent use in unhook.
* Not `NULL` (errno == 1): The hook cannot be executed because the target dynamic library has not been loaded. ShadowHook will record the current hook "demand" internally, and once the target dynamic library is loaded into the memory, the hook operation will be executed immediately. The return value is a stub, you can save the return value for subsequent use in unhook.
* `NULL`: The hook failed. You can call `shadowhook_get_errno` to get the errno, and you can continue to call `shadowhook_to_errmsg` to get the error message.

### Example

```C
void *orig;
void *stub = shadowhook_hook_sym_name("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_invoke, &orig);

int error_num = shadowhook_get_errno();
const char *error_msg = shadowhook_to_errmsg(error_num);
__android_log_print(ANDROID_LOG_WARN,  "test", "hook return: %p, %d - %s", stub, error_num, error_msg);
```

## 3. Execute hook by "library name + function name" (requires callback)

```C
#include "shadowhook.h"

typedef void (*shadowhook_hooked_t)(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, void *new_addr, void *orig_addr, void *arg);

void *shadowhook_hook_sym_name_callback(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr, shadowhook_hooked_t hooked, void *hooked_arg);
```

For the case of hook "dynamic library that has not been loaded into the process", sometimes we need to know "the execution result and execution time of the current hook request in the future". At this time, you can use `shadowhook_hook_sym_name_callback` to specify an additional callback, which will be called when the hook operation is executed in the future (or now).

### Parameters

* `hooked` (can pass `NULL` if not needed): When the hook is executed, the callback function is called. The definition of the callback function is `shadowhook_hooked_t`, where the first parameter is the errno executed by the hook, `0` means success, non-`0` failure (you can call `shadowhook_to_errmsg` to get the error message). Subsequent parameters in `shadowhook_hooked_t` correspond to the parameters in `shadowhook_hook_sym_name_callback`.
* `hooked_arg` (can pass `NULL` if not needed): The last parameter (`arg`) in the hooked callback function.
* Other parameters are the same as `shadowhook_hook_sym_name`.

### Return Value

* The meaning is the same as `shadowhook_hook_sym_name`.

### Example

```C
typedef void my_hooked_callback(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, void *new_addr, void *orig_addr, void *arg);
{
    const char *error_msg = shadowhook_to_errmsg(error_number);
    __android_log_print(ANDROID_LOG_WARN,  "test", "hooked: %s, %s, %d - %s", lib_name, sym_name, error_number, error_msg);
}

void do_hook(void)
{
    void *orig;
    void *stub = shadowhook_hook_sym_name_callback("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_invoke, &orig, my_hooked_callback, NULL);

    int error_num = shadowhook_get_errno();
    const char *error_msg = shadowhook_to_errmsg(error_num);
    __android_log_print(ANDROID_LOG_WARN,  "test", "hook return: %p, %d - %s", stub, error_num, error_msg);
}
```

## 4. unhook

```C
#include "shadowhook.h"

int shadowhook_unhook(void *stub);
```

### Parameter

* `stub` (must be specified): The stub value returned by the hook function.

### Return Value

* `0`: Unhook succeeded.
* `-1`: Unhook failed. You can call `shadowhook_get_errno` to get the errno, and you can continue to call `shadowhook_to_errmsg` to get the error message.

### Example

```C
int result = shadowhook_unhook(stub);
if(result != 0)
{
    int error_num = shadowhook_get_errno();
    const char *error_msg = shadowhook_to_errmsg(error_num);
    __android_log_print(ANDROID_LOG_WARN,  "test", "unhook failed: %d - %s", error_num, error_msg);
}
```


# Proxy Function

* The proxy function needs to be defined as the same type as the original function (parameter type + return value type).
* Regardless of shared mode or unique mode, the hook function will return the address of the original function through the `void **orig_addr` parameter.

## Proxy functions in shared mode

In shared mode. Inside the proxy function, use the `SHADOWHOOK_CALL_PREV` macro to call the original function. Outside the proxy function, call the original function via `orig_addr`.

### `SHADOWHOOK_CALL_PREV` macro

```C
#include "shadowhook.h"

#ifdef __cplusplus
#define SHADOWHOOK_CALL_PREV(func, ...) ...
#else
#define SHADOWHOOK_CALL_PREV(func, func_sig, ...) ...
#endif
```

* Used to call the original function in the proxy function. It is also possible not to call the original function in the proxy function, but please do not directly call the original function through the function name or `orig_addr`.
* The usage in the C++ source file is: the first parameter passes the address of the current proxy function, followed by each parameter of the function in sequence.
* The usage in the C source file is: the first parameter passes the address of the current proxy function, the second parameter passes the type definition of the function of the current hook, and then passes the parameters of the function in order.

### `SHADOWHOOK_POP_STACK` macro and `SHADOWHOOK_STACK_SCOPE` macro

```C
#include "shadowhook.h"

// pop stack in proxy-function (for C/C++)
#define SHADOWHOOK_POP_STACK() ...

// pop stack in proxy-function (for C++ only)
#define SHADOWHOOK_STACK_SCOPE() ...
```

ShadowHook's proxy function management mechanism accomplishes a few things:

* You can hook and unhook the same hook point multiple times without interfering with each other.
* Automatically avoid recursive calls and circular calls that may be formed between proxy functions (for example: `read` is called in `open_proxy` of SDK1, and `open` is called in `read_proxy` of SDK2).
* The call stack can be unwind in the normal way in the proxy function.

In order to do all of the above at the same time, you need to do something extra in the proxy function, namely "perform the stack cleanup inside the ShadowHook", which requires you to call the `SHADOWHOOK_POP_STACK` macro or `SHADOWHOOK_STACK_SCOPE` macro in the proxy function to complete (two options one). Note: Even if you do nothing in the proxy function, you need to "perform stack cleanup inside shadowhook".

* `SHADOWHOOK_POP_STACK` macro: for C and C++ source files. Need to make sure to call before the proxy function returns.
* `SHADOWHOOK_STACK_SCOPE` macro: for C++ source files. Call it once at the beginning of the proxy function.

### `SHADOWHOOK_RETURN_ADDRESS` macro

```C
#include "shadowhook.h"

// get return address in proxy-function
#define SHADOWHOOK_RETURN_ADDRESS() ...
```

Occasionally, you may need to get the value of LR through `__builtin_return_address(0)` in the proxy function. Since the trampoline in shared mode changes LR, calling `__builtin_return_address(0)` directly will return the address of the trampoline.

In the proxy function, the original LR needs to be obtained through the `SHADOWHOOK_RETURN_ADDRESS` macro.

### `SHADOWHOOK_ALLOW_REENTRANT` macro and `SHADOWHOOK_DISALLOW_REENTRANT` macro

```C
#include "shadowhook.h"

// allow reentrant of the current proxy-function
#define SHADOWHOOK_ALLOW_REENTRANT() ...

// disallow reentrant of the current proxy-function
#define SHADOWHOOK_DISALLOW_REENTRANT() ...
```

In shared mode, proxy functions are not allowed to be reentrant by default, because reentrancy may occur between multiple SDKs using ShadowHook, eventually forming an infinite loop of calls (for example, `open_proxy` in SDK1 call `read`, and `read_proxy` in SDK2 call `open`).

However, in some special usage scenarios, reentrancy controlled by business logic may be required, they will not form an "infinite" call loop, but will terminate when certain business conditions are met. If you confirm that this is the case for your use case, please call `SHADOWHOOK_ALLOW_REENTRANT` in the proxy function to allow reentrancy, and when the logic of the proxy function runs to "no longer need to allow reentrancy", you can call `SHADOWHOOK_DISALLOW_REENTRANT`.

### Example 1 (C source file)

```C
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void *malloc_proxy(size_t sz)
{
    if(sz > 1024)
    {
        // Perform stack cleanup (cannot be omitted).
        SHADOWHOOK_POP_STACK();
        return NULL;
    }

    // Call the original function.
    void *result = SHADOWHOOK_CALL_PREV(malloc_proxy, malloc_t, sz);

    // Perform stack cleanup (cannot be omitted).
    SHADOWHOOK_POP_STACK();
    return result;
} 

void do_hook(void)
{
    stub = shadowhook_hook_sym_addr(malloc, malloc_proxy, &orig);
}

void do_unhook(void)
{
    shadowhook_unhook(stub);
    stub = NULL;
}

void *my_malloc_4k(void)
{
    // In some scenarios, maybe you need to call the original function directly.
    return ((malloc_t)orig)(4096);
}
```

### Example 2 (C++ source file)

```C++
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void * malloc_proxy(size_t sz)
{
    // Perform stack cleanup (cannot be omitted), just call it once.
    SHADOWHOOK_STACK_SCOPE();
    
    if(sz > 1024)
        return nullptr;

    // Call the original function.
    return SHADOWHOOK_CALL_PREV(malloc_proxy, sz);
} 

void do_hook(void)
{
    stub = shadowhook_hook_sym_addr(malloc, malloc_proxy, &orig);
}

void do_unhook(void)
{
    shadowhook_unhook(stub);
    stub = NULL;
} 

void *my_malloc_4k(void)
{
    // In some scenarios, maybe you need to call the original function directly.
    return ((malloc_t)orig)(4096);
}
```

### Example 3 (controlling whether the proxy function is reentrant)

```C
int test_func_2(int a, int b)
{
    a--; // a is decremented by 1 each time
    return test_func_1(a, b);
}

int test_func_1(int a, int b)
{
    if(a < b)
        return 0;
    else
        return test_func_2(a, b);
}

void test(void)
{
    test_func_1(10, 5);
}
```

`test_func_1` and `test_func_2` seem to form an infinite loop, but when `a < b`, the loop will terminate, so it is not really an infinite loop. (The arguments to call `test_func_1` in the `test` function are `a = 10` and `b = 5`, decrement `a` by `1` each time `test_func_2`)

By default ShadowHook prevents the reentrancy of proxy functions, because reentrancy can easily lead to an infinite loop between proxy functions. But if this kind of reentrancy of proxy functions is what you need, please refer to the following example to use the `SHADOWHOOK_ALLOW_REENTRANT` macro and `SHADOWHOOK_DISALLOW_REENTRANT` macro to control the "reentrancy" of a code area in the proxy function:

The following code hooks `test_func_1`, which uses the `SHADOWHOOK_ALLOW_REENTRANT` macro to allow reentrancy.

```C++
void *stub;

int test_func_1_proxy(int a, int b)
{
    // Perform stack cleanup (cannot be omitted), just call it once.
    SHADOWHOOK_STACK_SCOPE();
    
    // Add your own business logic.
    if(a > 1024 || b > 1024)
        return -1;

    // Now to call the original function, 
    // we want every call to test_func_1 to go into our proxy function.
    SHADOWHOOK_ALLOW_REENTRANT();

    // Call the original function.
    int result = SHADOWHOOK_CALL_PREV(test_func_1_proxy, sz);
    
    // Next, we will continue to add some business logic, 
    // and want to restore ShadowHook's "preventing proxy function from being reentrant" protection function.
    SHADOWHOOK_DISALLOW_REENTRANT();
    
    // Continue to add some business logic.
    write_log(global_log_fd, "test_func_1 called with a=%d, b=%d", a, b);
    
    return result;
} 

void do_hook(void)
{
    stub = shadowhook_hook_sym_addr(test_func_1, test_func_1_proxy, NULL);
}

void do_unhook(void)
{
    shadowhook_unhook(stub);
    stub = NULL;
}
```

## Proxy function in unique mode

In unique mode. Please always call the original function through the original function address `orig_addr` returned by the hook function.

### Example

```C
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void *my_malloc(size_t sz)
{
    if(sz > 1024)
        return nullptr;

    // Call the original function.
    return ((malloc_t)orig)(sz);
}

void do_hook(void)
{
    stub = shadowhook_hook_sym_addr(malloc, my_malloc, &orig);
}

void do_unhook(void)
{
    shadowhook_unhook(stub);
    stub = NULL;
}
```


# Error Number

* `0`: Success.
* `1`: "Hook executed by library name + function name" cannot be completed because the dynamic library is not loaded, and the current hook request is in the pending state.
* Other: Various other errors.

## Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static String toErrmsg(int errno)
```

* `init` returns "non-`0`", indicating that initialization failed, and the return value of `init` is errno.
* The error message corresponding to errno can be obtained through `toErrmsg`.

## Native API

```C
#include "shadowhook.h"

int shadowhook_get_errno(void);
const char *shadowhook_to_errmsg(int error_number);
```

* `shadowhook_init` returns "non-`0`", which means initialization failed. At this point, the return value of `shadowhook_init` is errno.
* `shadowhook_hook_*` returns `NULL` for failure; `shadowhook_unhook` returns `-1` for failure. At this point, errno can be obtained through `shadowhook_get_errno`.
* The error message corresponding to errno can be obtained through `shadowhook_to_errmsg`.


# Operation Record

* ShadowHook will record the operation information of hook / unhook in memory.
* These operation records can be obtained at the java layer or native layer, and they are returned as strings, in row units, and comma-separated information items.
* The specific information items and order that can be returned are as follows:

| Order | Name | Description | Remark |
| :---- | :---- | :---- | :---- |
| 1 | TIMESTAMP | Timestamp | Format：YYYY-MM-DDThh:mm:ss.sss+hh:mm |
| 2 | CALLER\_LIB\_NAME | Caller dynamic library name | basename |
| 3 | OP | Operation type | hook\_sym\_addr / hook\_sym\_name / unhook / error |
| 4 | LIB_NAME | The name of the dynamic library where the target function is located | only valid for hooks |
| 5 | SYM_NAME | Target function name | only valid for hooks |
| 6 | SYM_ADDR | Target function address | only valid for hooks |
| 7 | NEW_ADDR | Proxy function address | only valid for hooks |
| 8 | BACKUP_LEN | The instruction length of the overridden function header | only valid for hooks。arm32：4 / 8 / 10；arm64：4 / 16 |
| 9 | ERRNO | Error number |  |
| 10 | STUB | The stub returned by the hook | pointer type value, hook and unhook can be paired by this value |

## Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static String getRecords(RecordItem... recordItems)

public enum RecordItem {
    TIMESTAMP,
    CALLER_LIB_NAME,
    OP,
    LIB_NAME,
    SYM_NAME,
    SYM_ADDR,
    NEW_ADDR,
    BACKUP_LEN,
    ERRNO,
    STUB
}
```

* If you call `getRecords` without passing any parameters, it means to get all the information items.
* If you only want to collect statistics about hook/unhook operation information and errno of some app, it is more convenient to use java API.

## Native API

```C
#include "shadowhook.h"

#define SHADOWHOOK_RECORD_ITEM_ALL             0x3FF // 0b1111111111
#define SHADOWHOOK_RECORD_ITEM_TIMESTAMP       (1 << 0)
#define SHADOWHOOK_RECORD_ITEM_CALLER_LIB_NAME (1 << 1)
#define SHADOWHOOK_RECORD_ITEM_OP              (1 << 2)
#define SHADOWHOOK_RECORD_ITEM_LIB_NAME        (1 << 3)
#define SHADOWHOOK_RECORD_ITEM_SYM_NAME        (1 << 4)
#define SHADOWHOOK_RECORD_ITEM_SYM_ADDR        (1 << 5)
#define SHADOWHOOK_RECORD_ITEM_NEW_ADDR        (1 << 6)
#define SHADOWHOOK_RECORD_ITEM_BACKUP_LEN      (1 << 7)
#define SHADOWHOOK_RECORD_ITEM_ERRNO           (1 << 8)
#define SHADOWHOOK_RECORD_ITEM_STUB            (1 << 9)

char *shadowhook_get_records(uint32_t item_flags);
void shadowhook_dump_records(int fd, uint32_t item_flags);
```

* Through the `item_flags` parameter, you can specify which information items need to be returned. When you need to return all information items, you can use `SHADOWHOOK_RECORD_ITEM_ALL`.
* The buffer returned by `shadowhook_get_records` is allocated with `malloc`, you need to use `free` to free it.
* `shadowhook_dump_records` will write the content to `fd`, this function is **async-signal safety**, you can use it in the signal handler. For example, after a native crash, call `shadowhook_dump_records` in the signal handler to write hook/unhook operation information to your own tombstone file.
