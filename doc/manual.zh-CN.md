# **ShadowHook 手册**

[ShadowHook Manual - English](manual.md)


# 介绍

**ShadowHook** 是一个 Android inline hook 库，它支持 thumb、arm32 和 arm64。

ShadowHook 现在被用于 TikTok，抖音，今日头条，西瓜视频，飞书中。

如果你需要的是 Android PLT hook 库，请移步到 [ByteHook](https://github.com/bytedance/bhook)。


# 特征

* 支持 Android 4.1 - 15 (API level 16 - 35)。
* 支持 armeabi-v7a 和 arm64-v8a。
* 支持针对函数整体的 hook，不支持对函数中间位置的 hook。
* 支持通过“函数地址”或“库名 + 函数名”的方式指定 hook 位置。
* 自动完成“新加载动态库”的 hook（仅限“库名 + 函数名”方式），hook 完成后调用可选的回调函数。
* 可对同一个 hook 点并发执行多个 hook 和 unhook，彼此互不干扰（仅限 shared 模式）。
* 自动避免代理函数之间可能形成的递归调用和环形调用（仅限 shared 模式）。
* 代理函数中支持以正常的方式（CFI，EH，FP）回溯调用栈。
* 集成符号地址查找功能。
* 使用 MIT 许可证授权。


# 接入

## 在 build.gradle 中增加依赖

ShadowHook 发布在 [Maven Central](https://search.maven.org/) 上。为了使用 [native 依赖项](https://developer.android.com/studio/build/native-dependencies)，ShadowHook 使用了从 [Android Gradle Plugin 4.0+](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#native-dependencies) 开始支持的 [Prefab](https://google.github.io/prefab/) 包格式。

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

**注意**：ShadowHook 使用 [prefab package schema v2](https://github.com/google/prefab/releases/tag/v2.0.0)，它是从 [Android Gradle Plugin 7.1.0](https://developer.android.com/studio/releases/gradle-plugin?buildsystem=cmake#7-1-0) 开始作为默认配置的。如果你使用的是 Android Gradle Plugin 7.1.0 之前的版本，请在 `gradle.properties` 中加入以下配置：

```
android.prefabVersion=2.0.0
```

## 在 CMakeLists.txt 或 Android.mk 中增加依赖

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

## 指定一个或多个你需要的 ABI

```Gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a'
        }
    }
}
```

## 增加打包选项

如果你是在一个 SDK 工程里使用 ShadowHook，你可能需要避免把 libshadowhook.so 打包到你的 AAR 里，以免 app 工程打包时遇到重复的 libshadowhook.so 文件。

```Gradle
android {
    packagingOptions {
        exclude '**/libshadowhook.so'
        exclude '**/libshadowhook_nothing.so'
    }
}
```

另一方面, 如果你是在一个 APP 工程里使用 ShadowHook，你可以需要增加一些选项，用来处理重复的 libshadowhook.so 文件引起的冲突。

```Gradle
android {
    packagingOptions {
        pickFirst '**/libshadowhook.so'
        pickFirst '**/libshadowhook_nothing.so'
    }
}
```


# 初始化

* **可以在 java 层或 native 层初始化，二选一即可。**
* java 层初始化逻辑实际上只做了两件事：`System.loadLibrary`、调用 native 层的 `init` 函数。
* 可以并发的多次的执行初始化，但只有第一次实际生效，后续的初始化调用将直接返回第一次初始化的返回值。

## 初始化参数

### 模式

* `shared` 模式（默认值）：可对同一个 hook 点并发执行多个 hook 和 unhook，彼此互不干扰。自动避免代理函数之间可能形成的递归调用和环形调用。建议复杂的机构或组织使用 shared 模式。
* `unique` 模式：同一个 hook 点只能被 hook 一次（unhook 后可以再次 hook）。需要自己处理代理函数之间可能形成的递归调用和环形调用。个人或小型的 app，或某些调试场景中（例如希望跳过 ShadowHook 的 proxy 管理机制，调试分析比较单纯的 inlinehook 流程），可以使用该模式。

### 调试日志

* `true`：开启。调试信息将写入 logcat。tag：`shadowhook_tag`。
* `false`（默认值）：关闭。

### 操作记录

* `true`：开启。将 hook 和 unhook 操作记录写入内部 buffer。这些操作记录信息可以通过其他 API 获取。
* `false`（默认值）：关闭。

## Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static int init()
public static int init(Config config)
```

返回 `0` 表示成功，非 `0` 表示失败（非 `0` 值为错误码）。

### 举例（使用默认初始化参数）

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init();
    }
}
```

### 举例（通过Config参数指定更多选项）

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init(new ShadowHook.ConfigBuilder()
            .setMode(ShadowHook.Mode.SHARED)
            .setDebuggable(true)
            .setRecordable(true)
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

返回 `0` 表示成功，非 `0` 表示失败（非 `0` 值为错误码）。

## 初始化相关的其他函数

`模式`在初始化时指定，之后不可修改。`调试日志`和`操作记录`可以在运行时随时开启和关闭。

Java API:

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static Mode getMode()

public static boolean getDebuggable()
public static void setDebuggable(boolean debuggable)

public static boolean getRecordable()
public static void setRecordable(boolean recordable)
```

Native API:

```C
#include "shadowhook.h"

shadowhook_mode_t shadowhook_get_mode(void);

bool shadowhook_get_debuggable(void);
void shadowhook_set_debuggable(bool debuggable);

bool shadowhook_get_recordable(void);
void shadowhook_set_recordable(bool recordable);
```

# 查找符号地址

* ShadowHook 的 hook API 支持通过“函数地址”或“库名 + 函数名”指定 hook 位置。
* NDK 暴露的 `.dynsym` 中的函数地址可以借助 linker 来获取，但是 `.symtab` 和 `.symtab in .gnu_debugdata` 中的函数地址需要通过其他手段来获取。

```C
#include "shadowhook.h"

void *shadowhook_dlopen(const char *lib_name);
void shadowhook_dlclose(void *handle);
void *shadowhook_dlsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_dynsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_symtab(void *handle, const char *sym_name);
```

* 这组 API 的用法类似于系统提供的 `dlopen`，`dlclose`，`dlsym`。
* `shadowhook_dlsym_dynsym` 只能查找 `.dynsym` 中的符号，速度较快。
* `shadowhook_dlsym_symtab` 能查找 `.symtab` 和 `.symtab in .gnu_debugdata` 中的符号，但是速度较慢。
* `shadowhook_dlsym` 会先尝试在 `.dynsym` 中查找符号，如果找不到，会继续尝试在 `.symtab` 和 `.symtab in .gnu_debugdata` 中查找。


# hook 和 unhook

* 支持通过“函数地址”或“库名 + 函数名”指定 hook 位置。
* 自动完成“新加载so库”的 hook（仅限“库名 + 函数名”方式），hook 完成后调用可选的回调函数。
* 只支持针对函数整体的 hook，不支持对函数中间位置的 hook。
* 如果要 hook 的函数不在 ELF 的符号表（`.dynsym` 或 `.symtab` 或 `.symtab in .gnu_debugdata`）中，则只能通过 `shadowhook_hook_func_addr` 来 hook，其他的 hook API 肯定无法 hook 成功。

## 1. 通过 “函数地址” hook 有符号信息的函数

```C
#include "shadowhook.h"

void *shadowhook_hook_sym_addr(void *sym_addr, void *new_addr, void **orig_addr);
```

这种方式只能 hook “当前已加载到进程中的动态库”。

用 `shadowhook_hook_sym_addr` hook 的函数（`sym_addr`）必须存在于 ELF 的符号表（`.dynsym` / `.symtab`）中。可以用 `readelf -sW` 确认。

由于 ELF 的符号表中包含函数的长度信息，所以 ShadowHook 可以用这个信息来确认“hook 时修改的函数头部长度不会超过函数总长度”，这样做能提高 hook 的稳定性。

### 参数

* `sym_addr`（必须指定）：需要被 hook 的函数的绝对地址。
* `new_addr`（必须指定）：新函数（proxy 函数）的绝对地址。
* `orig_addr`（不需要的话可传 `NULL`）：返回原函数地址。

### 返回值

* 非 `NULL`：hook 成功。返回值是个 stub，可保存返回值，后续用于 unhook。
* `NULL`：hook 失败。可调用 `shadowhook_get_errno` 获取 errno，可继续调用 `shadowhook_to_errmsg` 获取 error message。

### 举例

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

在这个例子中，`sym_addr` 是由 linker 在加载当前动态库时指定的。

## 2. 通过 “函数地址” hook 无符号信息的函数

**ShadowHook 从 1.0.4 版本开始提供 `shadowhook_hook_func_addr`。**

```C
#include "shadowhook.h"

void *shadowhook_hook_func_addr(void *func_addr, void *new_addr, void **orig_addr);
```

这种方式只能 hook “当前已加载到进程中的动态库”。

用 `shadowhook_hook_func_addr` hook 的函数（`func_addr`）可以不存在于 ELF 的符号表（`.dynsym` / `.symtab`）中。

此时需要使用者保证 `func_addr` 对应的函数足够长，不同架构和指令类型需要的函数长度如下：

| 架构 | 指令类型 | 最小函数长度（字节） | 理想函数长度（字节） |
| :--- | :--- | ---: | ---: |
| arm32 | thumb | 4 | 10 |
| arm32 | arm32 | 4 | 8 |
| arm64 | arm64 | 4 | 16 |

注意：这里的函数长度指函数编译后生成的二进制 CPU 指令序列的长度，可以用 `objdump` 等工具确认。

1. **实际函数长度** < 最小函数长度：arm32 和 arm64 肯定会出问题，thumb 可能会出问题。
2. 最小函数长度 <= **实际函数长度** < 理想函数长度：也许会出问题，也许不会。或者有时会出问题，有时不会。
3. 理想函数长度 <= **实际函数长度**：肯定不会出问题。（仅指由于指令覆盖长度超过函数总长度引发的问题）

由此可见，相对于 `shadowhook_hook_sym_addr` 来说，`shadowhook_hook_func_addr` 的可靠性较差。因此建议仅在“被 hook 函数的指令长度可控”的情况下使用，例如：

1. 通过 hook 某个无符号的函数，来修复特定机型特定 OS 版本的系统库 bug。
2. 需要 hook 的函数是由某个特定版本的 API 返回的。例如 hook 特定版本的 Unity，通过 vulkan 提供的 `vkGetInstanceProcAddr` 获取需要 hook 的函数地址。

在这些“被 hook 的 ELF 文件的版本已知和可控”的情况下，即使使用 `shadowhook_hook_func_addr` 来执行 hook，我们也可以预先在开发阶段验证 hook 的稳定性。

### 参数

* `func_addr`（必须指定）：需要被 hook 的函数的绝对地址。
* `new_addr`（必须指定）：新函数（proxy 函数）的绝对地址。
* `orig_addr`（不需要的话可传 `NULL`）：返回原函数地址。

### 返回值

* 非 `NULL`：hook 成功。返回值是个 stub，可保存返回值，后续用于 unhook。
* `NULL`：hook 失败。可调用 `shadowhook_get_errno` 获取 errno，可继续调用 `shadowhook_to_errmsg` 获取 error message。

### 举例

```C
void *orig;
void *func = get_hidden_func_addr();
void *stub = shadowhook_hook_func_addr(func, my_func, &orig);
if(stub == NULL)
{
    int error_num = shadowhook_get_errno();
    const char *error_msg = shadowhook_to_errmsg(error_num);
    __android_log_print(ANDROID_LOG_WARN,  "test", "hook failed: %d - %s", error_num, error_msg);
}
```

在这种方式中，`func_addr` 是由一个外部函数返回的，它在 ELF 中没有符号信息。

## 3. 通过 “库名 + 函数名” hook 函数

```C
#include "shadowhook.h"

void *shadowhook_hook_sym_name(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr);
```

* 这种方式可以 hook “当前已加载到进程中的动态库”，也可以 hook “还没有加载到进程中的动态库”（如果 hook 时动态库还未加载，ShadowHook 内部会记录当前的 hook “诉求”，后续一旦目标动态库被加载到内存中，将立刻执行 hook 操作）。
* ShadowHook 可以 hook ELF 中 `.dynsym` / `.symtab` / `.symtab in .gnu_debugdata` 中的符号，`shadowhook_hook_sym_name` 会完成符号地址查找的工作。
* 可以使用 `readelf` 查看 `.dynsym` / `.symtab` 中的符号。
* 可以使用 `dd` + `xz` + `readelf` 查看 `.symtab in .gnu_debugdata` 中的符号。
* 如果需要 hook “多个来自同一个动态库的符号”，建议使用 `shadowhook_dl*` API 来批量完成符号地址查找，这样可以加快 hook 流程中符号查找的速度。

### 参数
* `lib_name`（必须指定）：符号所在 ELF 的 basename 或 pathname。对于在进程中确认唯一的动态库，可以只传 basename，例如：`libart.so`。对于不唯一的动态库，需要根据安卓版本和 arch 自己处理兼容性，例如：`/system/lib64/libbinderthreadstate.so` 和 `/system/lib64/vndk-sp-29/libbinderthreadstate.so`。否则，ShadowHook 只会 hook 进程中第一个匹配到 basename 的动态库。
* `sym_name`（必须指定）：符号名。
* `new_addr`（必须指定）：新函数（proxy 函数）的绝对地址。
* `orig_addr`（不需要的话可传 `NULL`）：返回原函数地址。

### 返回值

* 非 `NULL`（errno == 0）：hook 成功。返回值是个 stub，可保存返回值，后续用于 unhook。
* 非 `NULL`（errno == 1）：由于目标动态库还没有加载，导致 hook 无法执行。ShadowHook 内部会记录当前的 hook “诉求”，后续一旦目标动态库被加载到内存中，将立刻执行 hook 操作。返回值是个 stub，可保存返回值，后续用于 unhook。
* `NULL`：hook 失败。可调用 `shadowhook_get_errno` 获取 errno，可继续调用 `shadowhook_to_errmsg` 获取 error message。

### 举例

```C
void *orig;
void *stub = shadowhook_hook_sym_name("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_invoke, &orig);

int error_num = shadowhook_get_errno();
const char *error_msg = shadowhook_to_errmsg(error_num);
__android_log_print(ANDROID_LOG_WARN,  "test", "hook return: %p, %d - %s", stub, error_num, error_msg);
```

## 4. 通过“库名 + 函数名”执行 hook（需要 callback）

```C
#include "shadowhook.h"

typedef void (*shadowhook_hooked_t)(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, void *new_addr, void *orig_addr, void *arg);

void *shadowhook_hook_sym_name_callback(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr, shadowhook_hooked_t hooked, void *hooked_arg);
```

对于 hook “还没有加载到进程中的动态库”的情况，有时我们需要知道“当前 hook 诉求在未来的执行结果和执行时刻”。这时可以使用 `shadowhook_hook_sym_name_callback` 来额外指定一个 callback，当 hook 操作在未来（或此刻）被执行时，会调用这里的 callback。

### 参数
* `hooked`（不需要的话可传 `NULL`）：当 hook 被执行后，调用该回调函数。回调函数的定义是`shadowhook_hooked_t`，其中第一个参数是 hook 执行的 errno，`0` 表示成功，非 `0` 失败（可调用 `shadowhook_to_errmsg` 获取 error message）。`shadowhook_hooked_t` 中后续参数对应 `shadowhook_hook_sym_name_callback` 中的各个参数。
* `hooked_arg`（不需要的话可传 `NULL`）：hooked 回调函数中的最后一个参数（`arg`）。
* 其他参数和 `shadowhook_hook_sym_name` 相同。

### 返回值

* 含义和 `shadowhook_hook_sym_name` 相同。

### 举例

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

## 5. unhook

```C
#include "shadowhook.h"

int shadowhook_unhook(void *stub);
```

### 参数

* `stub`（必须指定）：hook 函数返回的 stub 值。

### 返回值

* `0`：unhook 成功。
* `-1`：unhook 失败。可调用 `shadowhook_get_errno` 获取 errno，可继续调用 `shadowhook_to_errmsg` 获取 error message。

### 举例

```C
int result = shadowhook_unhook(stub);
if(result != 0)
{
    int error_num = shadowhook_get_errno();
    const char *error_msg = shadowhook_to_errmsg(error_num);
    __android_log_print(ANDROID_LOG_WARN,  "test", "unhook failed: %d - %s", error_num, error_msg);
}
```


# 代理函数

* 代理函数需要定义成和原函数同样的类型（参数类型 + 返回值类型）。
* 无论 shared 模式还是 unique 模式，hook 函数都会通过 `void **orig_addr` 参数将原函数地址返回。

## shared 模式中的代理函数

shared 模式中。在代理函数内部，请通过 `SHADOWHOOK_CALL_PREV` 宏调用原函数。在代理函数外部，请通过 `orig_addr` 调用原函数。

### `SHADOWHOOK_CALL_PREV` 宏

```C
#include "shadowhook.h"

#ifdef __cplusplus
#define SHADOWHOOK_CALL_PREV(func, ...) ...
#else
#define SHADOWHOOK_CALL_PREV(func, func_sig, ...) ...
#endif
```

* 用于在代理函数中调用原函数。在代理函数中也可以不调用原函数，但请不要通过函数名或 `orig_addr` 来直接调用原函数。
* 在 C++ 源文件中的用法是：第一个参数传递当前的代理函数的地址，后面按照顺序依次传递函数的各个参数。
* 在 C 源文件中的用法是：第一个参数传递当前的代理函数的地址，第二个参数传递当前 hook 的函数的类型定义，后面按照顺序依次传递函数的各个参数。

### `SHADOWHOOK_POP_STACK` 宏和 `SHADOWHOOK_STACK_SCOPE` 宏

```C
#include "shadowhook.h"

// pop stack in proxy-function (for C/C++)
#define SHADOWHOOK_POP_STACK() ...

// pop stack in proxy-function (for C++ only)
#define SHADOWHOOK_STACK_SCOPE() ...
```

ShadowHook 的代理函数管理机制完成了一些事情：

* 可对同一个 hook 点多次 hook 和 unhook，彼此互不干扰。
* 自动避免代理函数之间可能形成的递归调用和环形调用（比如：SDK1 的 `open_proxy` 中调用了 `read`，SDK2 的 `read_proxy` 中又调用了 `open`）。
* 代理函数中可以用常规的方式回溯调用栈。

为了同时做到上面这些，需要在代理函数中做一些额外的事情，即“执行 ShadowHook 内部的 stack 清理”，这需要你在 proxy 函数中调用 `SHADOWHOOK_POP_STACK` 宏或 `SHADOWHOOK_STACK_SCOPE` 宏来完成（二选一）。注意：即使你在代理函数中什么也不做，也需要“执行 ShadowHook 内部的 stack 清理”。

* `SHADOWHOOK_POP_STACK` 宏：适用于 C 和 C++ 源文件。需要确保在代理函数返回前调用。
* `SHADOWHOOK_STACK_SCOPE` 宏：适用于 C++ 源文件。在代理函数开头调用一次即可。

### `SHADOWHOOK_RETURN_ADDRESS` 宏

```C
#include "shadowhook.h"

// get return address in proxy-function
#define SHADOWHOOK_RETURN_ADDRESS() ...
```

偶尔，你可能需要在代理函数中通过 `__builtin_return_address(0)` 获取 LR 的值，由于 shared 模式中 trampoline 改变了 LR，直接调用 `__builtin_return_address(0)` 将返回 trampoline 的地址。

在代理函数中，需要通过 `SHADOWHOOK_RETURN_ADDRESS` 宏来获取原始的 LR。

### `SHADOWHOOK_ALLOW_REENTRANT` 宏和 `SHADOWHOOK_DISALLOW_REENTRANT` 宏

```C
#include "shadowhook.h"

// allow reentrant of the current proxy-function
#define SHADOWHOOK_ALLOW_REENTRANT() ...

// disallow reentrant of the current proxy-function
#define SHADOWHOOK_DISALLOW_REENTRANT() ...
```

在 shared 模式中，默认是不允许 proxy 函数被重入的，因为重入可能发生在多个使用 ShadowHook 的 SDK 之间，最终形成了一个无限循环的调用环（比如：SDK1 的 `open_proxy` 中调用了 `read`，SDK2 的 `read_proxy` 中又调用了 `open`）。

但是，某些特殊使用场景中，由业务逻辑控制的重入可能是需要的，他们并不会形成“无限的”调用环，而是会在某些业务条件满足时终止。如果你确认你的使用场景是这种情况，请在 proxy 函数中调用 `SHADOWHOOK_ALLOW_REENTRANT` 以允许重入，当 proxy 函数的逻辑运行到“不再需要允许重入的部分”时，可以调用 `SHADOWHOOK_DISALLOW_REENTRANT`。

### 举例一（C源文件）

```C
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void *malloc_proxy(size_t sz)
{
    if(sz > 1024)
    {
        // 执行 stack 清理（不可省略）
        SHADOWHOOK_POP_STACK();
        return NULL;
    }

    // 调用原函数
    void *result = SHADOWHOOK_CALL_PREV(malloc_proxy, malloc_t, sz);

    // 执行 stack 清理（不可省略）
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
    // 在某些场景中，也许你需要直接调用原函数。
    return ((malloc_t)orig)(4096);
}
```

### 举例二（C++源文件）

```C++
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void * malloc_proxy(size_t sz)
{
    // 执行 stack 清理（不可省略），只需调用一次
    SHADOWHOOK_STACK_SCOPE();
    
    if(sz > 1024)
        return nullptr;

    // 调用原函数
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
    // 在某些场景中，也许你需要直接调用原函数。
    return ((malloc_t)orig)(4096);
}
```

### 举例三（控制 proxy 函数是否可重入）

```C
int test_func_2(int a, int b)
{
    a--; // a 每次递减 1
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

`test_func_1` 和 `test_func_2` 看似会形成一个无限循环的环形调用，但是当 `a < b` 时，循环会终止，所以并不会真的死循环。（`test` 函数中调用 `test_func_1` 的参数是 `a = 10` 和 `b = 5`，每次 `test_func_2` 中将 `a` 递减 `1`）

默认情况下 ShadowHook 会阻止 proxy 函数的重入，因为重入很容易导致 proxy 函数之间形成死循环。但如果这种 proxy 函数的重入正是你所需要的，请参考下面的例子用 `SHADOWHOOK_ALLOW_REENTRANT` 宏和 `SHADOWHOOK_DISALLOW_REENTRANT` 宏来控制 proxy 函数中某个代码区域的“可重入性”：

下面的代码hook `test_func_1`，其中使用 `SHADOWHOOK_ALLOW_REENTRANT` 宏来允许重入。

```C++
void *stub;

int test_func_1_proxy(int a, int b)
{
    // 执行 stack 清理（不可省略），只需调用一次
    SHADOWHOOK_STACK_SCOPE();
    
    // 加点自己的业务逻辑
    if(a > 1024 || b > 1024)
        return -1;

    // 下面开始调用原函数了，我们希望每次对 test_func_1 的调用都走入我们的代理函数中。
    SHADOWHOOK_ALLOW_REENTRANT();

    // 调用原函数
    int result = SHADOWHOOK_CALL_PREV(test_func_1_proxy, sz);
    
    // 下面要继续加点业务逻辑，想恢复 ShadowHook 的“防止 proxy 函数被重入”的保护功能。
    SHADOWHOOK_DISALLOW_REENTRANT();
    
    // 继续加点业务逻辑
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

## unique模式中的代理函数

unique 模式中。请始终通过 hook 函数返回的原函数地址 `orig_addr` 调用原函数。

### 举例

```C
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void *my_malloc(size_t sz)
{
    if(sz > 1024)
        return nullptr;

    // 调用原函数
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
# 其他函数
```C
#include "shadowhook.h"

// register and unregister callbacks for executing dynamic library's .init .init_array / .fini .fini_array
typedef void (*shadowhook_dl_info_t)(struct dl_phdr_info *info, size_t size, void *data);
int shadowhook_register_dl_init_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int shadowhook_unregister_dl_init_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int shadowhook_register_dl_fini_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int shadowhook_unregister_dl_fini_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
```
注册 / 反注册 `soinfo::call_constructors` 和`soinfo::call_destructors`回调函数

### 参数

* `pre`（不需要的话可传 `NULL`）：调用`call_constructors/call_destructors `前的代理函数。
* `post`（不需要的话可传 `NULL`）：调用`call_constructors/call_destructors `后的代理函数。
* `data`（不需要的话可传 `NULL`）：`pre`/`post`代理函数中传递的数据。

注意：**`pre`/`post`代理函数不可同时为空**。

### 返回值

* `0`：成功。
* `-1`：失败。可调用 `shadowhook_get_errno` 获取 errno，可继续调用 `shadowhook_to_errmsg` 获取 error message。

### 举例

```C
#include "shadowhook.h"

static void dl_init_pre(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size, (void)data;
  __android_log_print(ANDROID_LOG_WARN,  "test", "dl_init, load_bias %" PRIxPTR ", %s", (uintptr_t)info->dlpi_addr, info->dlpi_name);
}

static void dl_fini_post(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size, (void)data;
  __android_log_print(ANDROID_LOG_WARN,  "test", "dl_fini, load_bias %" PRIxPTR ", %s", (uintptr_t)info->dlpi_addr, info->dlpi_name);
}

void register_callback(void) {
    if (0 != shadowhook_register_dl_init_callback(dl_init_pre, NULL, NULL)) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        __android_log_print(ANDROID_LOG_WARN,  "test", "dl_init failed: %d - %s", error_num, error_msg);
    }

    if (0 != shadowhook_register_dl_fini_callback(NULL, dl_fini_post, NULL)) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        __android_log_print(ANDROID_LOG_WARN,  "test", "dl_fini failed: %d - %s", error_num, error_msg);
    }
}
```

# 错误码

* `0`：成功。
* `1`：“通过库名 + 函数名执行的 hook”由于动态库没有加载，而无法完成，当前 hook 诉求处于 pending 状态。
* 其他：各种其他错误。

## Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static String toErrmsg(int errno)
```

* `init` 返回“非 `0`”，表示初始化失败，此时 `init` 的返回值就是 errno。
* 通过 `toErrmsg` 可以获取 errno 对应的 error message。

## Native API

```C
#include "shadowhook.h"

int shadowhook_get_errno(void);
const char *shadowhook_to_errmsg(int error_number);
```

* `shadowhook_init` 返回“非 `0`”，表示初始化失败。此时 `shadowhook_init` 的返回值就是 errno。
* `shadowhook_hook_*` 返回 `NULL` 表示失败；`shadowhook_unhook` 返回 `-1` 表示失败。此时通过 `shadowhook_get_errno` 可以获取到 errno。
* 通过 `shadowhook_to_errmsg` 可以获取 errno 对应的 error message。


# 操作记录

* ShadowHook 会在内存中记录 hook / unhook 的操作信息。
* 可以在 java 层或 native 层获取这些操作记录，它们以字符串形式返回，以行为单位，以逗号分隔信息项。
* 具体可以返回的信息项和顺序如下：

| 顺序 | 名称 | 描述 | 备注 |
| :---- | :---- | :---- | :---- |
| 1 | TIMESTAMP | 时间戳 | 格式：YYYY-MM-DDThh:mm:ss.sss+hh:mm |
| 2 | CALLER\_LIB\_NAME | 调用者动态库名称 | basename |
| 3 | OP | 操作类型 | hook\_sym\_addr / hook\_sym\_name / unhook / error |
| 4 | LIB_NAME | 目标函数所在动态库名称 | 仅 hook 有效 |
| 5 | SYM_NAME | 目标函数名称 | 仅 hook 有效 |
| 6 | SYM_ADDR | 目标函数地址 | 仅 hook 有效 |
| 7 | NEW_ADDR | proxy函数地址 | 仅 hook 有效 |
| 8 | BACKUP_LEN | 覆盖的函数头部的指令长度 | 仅 hook 有效。arm32：4 / 8 / 10；arm64：4 / 16 |
| 9 | ERRNO | 错误码 |  |
| 10 | STUB | hook 返回的 stub | 是个指针类型的数值，hook 和 unhook 可以通过这个值来配对 |

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

* 如果调用 `getRecords` 时不传递任何参数，则表示获取所有的信息项。
* 如果你只想收集统计一些 app 的 hook / unhook 操作信息以及 errno，那么使用 java API 会更方便。

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

* 通过 `item_flags` 参数可指定需要返回哪些信息项，需要返回所有信息项时可使用 `SHADOWHOOK_RECORD_ITEM_ALL`。
* `shadowhook_get_records` 返回的 buffer 是用 `malloc` 分配的，你需要用 `free` 来释放它。
* `shadowhook_dump_records` 会将内容写入 `fd` 中，这个函数是**异步信号安全**的，你可以在信号处理函数中使用它。例如，在发生native崩溃后，在信号处理函数中调用 `shadowhook_dump_records`，将 hook / unhook 操作信息写入你自己的 tombstone 文件中。
