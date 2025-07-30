# **shadowhook 手册**

[shadowhook Manual - English](manual.md)


# 介绍

**shadowhook 是一个 Android inline hook 库。** 它的目标是：

- **稳定** - 可以稳定的用于 production app 中。
- **兼容** - 始终保持新版本 API 和 ABI 向后兼容。
- **性能** - 持续降低 API 调用耗时和 hook 后引入的额外运行时耗时。
- **功能** - 除了基本的 hook 功能以外，还提供“hook 引起或相关”问题的通用解决方案。

> 如果你需要的是 Android PLT hook 库，建议试试 [ByteHook](https://github.com/bytedance/bhook)。


# 特征

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


# 接入

## 在 build.gradle 中增加依赖

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


# 初始化

> [!IMPORTANT]
> - 在调用 shadowhook API 之前需要先执行一次初始化。
> - 可以在 java 层或 native 层初始化，二选一即可。
> - java 层初始化逻辑实际上只做了两件事：调用 `System.loadLibrary()`，调用 native 层的 `shadowhook_init()` 初始化函数。
> - 在一个进程中，shadowhook 可以被多次初始化，但只有第一次实际生效，后续的初始化调用将直接返回第一次初始化的返回值。所以使用 shadowhook 的 SDK 各自自行初始化即可，只要初始化参数一致，最终集成到 app 时不会有冲突。

## 初始化API

初始化时可以指定 4 个参数（后面会详细解释），分别是：

- `mode`：默认 hook 模式（默认值：shared 模式）。
- `debuggable`：是否开启调试日志（默认值：`false`）。
- `recordable`：是否开启操作记录（默认值：`false`）。
- `disable`：是否禁用 shadowhook（默认值：`false`）。

### Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static int init()
public static int init(Config config)
```

**参数**
- `config`：配置参数集合。

**返回值**
- `0`：成功
- 非 `0`：失败（非 `0` 值为错误码）

**举例**

使用默认初始化参数：

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init();
    }
}
```

通过 `Config` 参数指定更多选项：

```Java
import com.bytedance.shadowhook.ShadowHook;

public class MySdk {
    public static void init() {
        ShadowHook.init(new ShadowHook.ConfigBuilder()
            .setMode(ShadowHook.Mode.SHARED)
            .setDebuggable(false)
            .setRecordable(false)
            .setDisable(false)
            .build());
    }
}
```

### Native API

```C
#include "shadowhook.h"

typedef enum
{
    SHADOWHOOK_MODE_SHARED = 0,
    SHADOWHOOK_MODE_UNIQUE = 1
} shadowhook_mode_t;

int shadowhook_init(shadowhook_mode_t mode, bool debuggable);
```

**参数**
- `mode`：默认 hook 模式。
- `debuggable`：是否开启调试日志。

**返回值**
- `0`：成功
- 非 `0`：失败（非 `0` 值为错误码）

## 初始化参数及相关 API

### 默认 hook 模式

通用的 inline hook 库除了完成基本的 inline hook 功能以外，还面对更多的业务挑战，比如：

- 从进程和 app 的角度看，inline hook 是一种对全局资源的修改。同一个地址可能被多个 SDK 多次 hook，这些 SDK 随时在发布新版本，各种 hook 功能云控开启，SDK 彼此之间不知道对方 hook 了哪些函数。假设：SDK1 hook 了 `open()` 函数，在 `open_proxy()` 中调用了 `read()`，SDK2 hook 了 `read()`，在 `read_proxy()` 中调用了 `open()`。于是当 SDK1 和 SDK2 都集成到 app 中时，`open_proxy()` 和 `read_proxy()` 之间会发生环形的递归调用。
- 同一个地址可能随时被多次 hook 和 unhook，这些操作彼此独立，都需要立刻完成。inline hook 库需要维持这些代理函数之间的链式调用关系。
- 对于安全合规、隐私监控类 SDK，需要保证自己的 hook 代理函数是唯一的，必须不受干扰的被执行。但是，“链式调用关系”中的上一个代理函数可能“随意修改输入参数”、或者“在某些情况下不调用链条中的下一个代理函数”。

以上这些问题，单个 inline hook 库的使用者（SDK）无法独立解决。shadowhook 引入了“模式”的概念，针对不同的需求，来解决（或缓解）这些问题。

- “模式”不是全局统一的，而是针对“代理函数”的，即：每个 hook 的代理函数都有独立的“模式”。在每次执行 hook 操作时，可以指定一个独立的模式（也可以不指定模式，此时会使用初始化时设置的“默认 hook 模式”）。
- “默认 hook 模式”只能在初始化时设置，一旦设置，没有其他 API 可以再修改。
- shadowhook 一共提供了 3 种 hook 模式：shared、multi、unique。

1. **shared 模式**（初始化时的默认值）
  - 支持对同一个 hook 点多次 hook 和 unhook，彼此互不干扰。
  - 自动避免代理函数之间形成的递归环形调用。
2. **multi 模式**
  - 支持对同一个 hook 点多次 hook 和 unhook，彼此互不干扰。
  - **不能**自动避免代理函数之间形成的递归环形调用。
  - **hook 完成后，代理函数的执行性能优于 shared 模式。**
  - **在同一个 hook 目标地址上，“shared 模式的代理函数”和“multi 模式的代理函数”可以共存。**
3. **unique 模式**
  - 在同一个 hook 目标地址上，只能被 hook 一次（unhook 后可以再次被 hook），因此无法与 shared 或 multi 模式共存，也无法与其他 unique 模式的 hook 代理函数共存。
  - **不能**自动避免代理函数之间形成的递归环形调用。
  - hook 完成后，代理函数的执行性能与 multi 模式一致。

> [!TIP]
> **最佳实践**
> 
> 在 shadowhook 1.1.1 以及之前的版本中，模式是全局的（所有 hook 代理函数使用同样的模式），如果你之前使用的是“默认的 shared 模式”，那么：
> 
> - 对于性能不敏感的 hook 点：维持现状即可。
> - 对于性能敏感的 hook 点：可以考虑改为“在 hook 时指定使用 multi 模式”，并验证是否有性能收益。
> - 对于安全合规、隐私监控类 SDK：建议评估是否有必要改用 unique 模式。

### 调试日志

- `true`：开启。调试信息写入 logcat。tag：`shadowhook_tag`
- `false`（默认值）：关闭。

初始化之后，可以再随时开/关调试日志。

**Java API**

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static boolean getDebuggable()
public static void setDebuggable(boolean debuggable)
```

**Native API**

```C
#include "shadowhook.h"

bool shadowhook_get_debuggable(void);
void shadowhook_set_debuggable(bool debuggable);
```

调试日志对性能影响较大，建议在 release 版本中关闭。

### 操作记录

- `true`：开启。将 hook、unhook、intercept、unintercept 的操作记录写入内部 buffer。这些操作记录可以通过其他 API 获取。
- `false`（默认值）：关闭。

初始化之后，可以再随时开/关操作记录。

**Java API**

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static boolean getRecordable()
public static void setRecordable(boolean recordable)
```

**Native API**

```C
#include "shadowhook.h"

bool shadowhook_get_recordable(void);
void shadowhook_set_recordable(bool recordable);
```

操作记录对性能影响较小，建议根据实际需要决定是否开启。

### 禁用 shadowhook

- `true`：禁用。
- `false`（默认值）：不禁用。

如果在初始化时设置了禁用 shadowhook，初始化实际只会执行 `System.loadLibrary()` 操作，然后设置一个全局的 disabled 标识，后续所有的其他 shadowhook API 调用都将失败。

在初始化之前，也可以先设置 disabled 标识。当通过这些 API 先禁用了 shadowhook 后，再执行初始化时，就会初始化失败。

**Java API**

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static boolean getDisable()
public static void setDisable(boolean disable)
```

**Native API**

```C
#include "shadowhook.h"

bool shadowhook_get_disable(void);
void shadowhook_set_disable(bool disable);
```

如果要完全禁用 shadowhook，需要在“其他任何对 shadowhook 的正常初始化”之前，先调用：

```Java
ShadowHook.setDisable(true);
```

或抢先初始化，同时设置 disable 为 `true`：

```Java
ShadowHook.init(new ShadowHook.ConfigBuilder().setDisable(true).build());
```

如果一旦“对 shadowhook 的正常初始化”顺利完成，再调用上述的 API 来禁用 shadowhook 的话，只能让后续的 hook 和 intercept 等 API 调用失败，但之前已经完成的 hook 和 intercept 依然会正常运行。

> [!TIP]
> **最佳实践**
> 
> 一般情况下，SDK不需要使用“禁用 shadowhook”功能，以免干扰其他 SDK 的功能。这个功能的典型使用场景是：app 灰度期间，云控是否“禁用 shadowhook”，用于“排查疑难崩溃是否和 inline hook 相关”。


# 符号

## 概述

通过符号名来定位 inlinehook 的位置是很常用的方法，好处是兼容性有保障。通过符号名获取到函数地址后，也可以直接调用这个函数。

在通过符号名查找符号地址时，请注意：

- C++ 函数符号名是指经过了 C++ mangler 之后的字符串（其中包含了参数类型等信息），C 函数符号名就是函数名本身。
- 同一个 C++ 系统函数名，在不同的 AOSP 版本中可能函数类型（参数，返回值）会有变化，导致符号名不同。hook 时要注意针对不同的 AOSP 版本分开定义符号名。
- 相同函数类型的 C++ 函数，在 32 位和 64 位 ELF 中的符号名可能会不同，因为比如 `size_t` 在 32 位中是 4 字节，在 64 位中是 8 字节，而参数类型是 C++ 函数符号名的一部分。hook 时要注意针对 32 位和 64 位分开定义符号名。
- 工具显示的符号名可能会有后缀，比如：`readelf -s libc.so | grep memcpy` 可能会看到 `memcpy@@LIBC`，符号名是 `memcpy`。
- LLVM 在 LTO 等操作时可能会给 `.symtab` 中的 Local 符号（调试符号）增加一个或多个随机的哈希值后缀，这些后缀以 `.` 或 `$` 开头。从ELF的角度来看，这些后缀也是符号名的一部分，比如：libart.so 中的符号 `_ZN3artL12IsStringInitERKNS_11InstructionEPNS_9ArtMethodE.__uniq.113072803961313432437294106468273961305`，但从 C++ 函数名的角度来看，它的符号是 `_ZN3artL12IsStringInitERKNS_11InstructionEPNS_9ArtMethodE`，C++ demangler 之后是 `art::IsStringInit(art::Instruction const&, art::ArtMethod*)`。在使用 shadowhook 查找符号地址或 hook 时，只需传入 `_ZN3artL12IsStringInitERKNS_11InstructionEPNS_9ArtMethodE` 即可，shadowhook 支持在查找符号地址时忽略 LLVM 添加的随机的哈希值后缀。
- 有些 ELF 中的符号名比较特殊，比如 linker，会给所有符号添加 `__dl_` 前缀。比如：linker ELF 中的符号名 `__dl___loader_cfi_fail` 对应源码中的 `__loader_cfi_fail` 函数。在使用 shadowhook 查找符号时，需要传入 `__dl___loader_cfi_fail`。
- 在同一个 AOSP 版本的不同厂商的设备中，可能由于厂商修改了编译参数，或者修改了一些函数，导致在某些系统库中，原本没有被 inline 的函数现在被 inline 了，于是在 ELF 产物中，它在 `.symtab` 中的符号也一起消失了。这时需要注意做兼容性处理。

## 符号信息所在的 ELF section

符号信息可能存在于 ELF 的以下 3 个 section 中：

1. **`.dynsym` section**：这里保存的是“参与动态链接过程的符号”，linker 加载 ELF 时在 relocate 过程中会用到这些信息。`.dynsym` 中的符号分为“导入符号”和“导出符号”，导入符号是当前ELF需要调用的“存在于外部 ELF 中的符号”（比如：你的 ELF libmy.so 使用了 libc.so 的 `memcpy`，则 `memcpy` 是 libmy.so 的导入符号），导出符号是存在于当前 ELF 中的可被外部（或内部）调用的符号（比如 `memcpy` 是 libc.so 的导出符号）。导出符号是可以被 inlinehook 的。
2. **`.symtab` section**：这是个大杂烩，其中包含了 Local 符号，这些符号不参与动态链接过程，它们只用于“调试器”和“崩溃或 anr 时获取 backtrace 中的符号名”。你可能会觉得 `.dynsym` 是 `.symtab` 的“子集”，事实上不是，它们相互不具有包含关系，这一点已经在很多线上设备中的 ELF 中验证过。
3. **`.gnu_debugdata` section**：由于 `.symtab` section 体积可能比较大，为了减小 ELF 体积，它可能被用 LZMA 压缩后存放在 `.gnu_debugdata` section 中。

## 查看 ELF 中的符号信息

> [!WARNING]
> 分析 Android ELF 时，请使用 Android NDK 中的 LLVM binutils，Linux / Mac 系统中的 binutils 可能存在兼容性问题。

1. 查看 `.dynsym` 和 `.symtab` 中的导出符号

```Shell
llvm-readelf -sW ./libXXX.so | grep -v " UND "
```

2. 查看 `.gnu_debugdata` 中的导出符号

先确认是否存在 `.gnu_debugdata` section

```Shell
llvm-readelf -S ./libXXX.so | grep ".gnu_debugdata"
```

如果有

```Shell
llvm-objcopy --dump-section .gnu_debugdata=libXXX_gnu_debugdata.so.xz ./libXXX.so
xz -d -k libXXX_gnu_debugdata.so.xz
llvm-readelf -sW ./libXXX_gnu_debugdata.so | grep -v " UND "
```

## 使用 shadowhook 查询符号地址

shadowhook 集成了开源库 [xDL](https://github.com/hexhacking/xDL)，除了用于满足自身的符号查询需求，还对外提供了一组 API：

```C
#include "shadowhook.h"

void *shadowhook_dlopen(const char *lib_name);
void shadowhook_dlclose(void *handle);
void *shadowhook_dlsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_dynsym(void *handle, const char *sym_name);
void *shadowhook_dlsym_symtab(void *handle, const char *sym_name);
```

- 这组 API 的用法十分明确，类似于系统提供的 `dlopen()`，`dlclose()`，`dlsym()`。
- `shadowhook_dlsym_dynsym()` 只能查找 `.dynsym` 中的符号，速度较快。
- `shadowhook_dlsym_symtab()` 能查找 `.symtab` 和 `.symtab in .gnu_debugdata` 中的符号，但是速度较慢。
- `shadowhook_dlsym()` 会先尝试在 `.dynsym` 中查找符号，如果找不到，会继续尝试在 `.symtab` 和 `.symtab in .gnu_debugdata` 中查找。

如果能精确的知道符号存在于 `.dynsym` 或 `.symtab` 中，并且确认“在不同厂商的设备上”都能保持这个特性，那么使用 `shadowhook_dlsym_dynsym()` 或 `shadowhook_dlsym_symtab()` 是速度最快的方式。否则可以使用 `shadowhook_dlsym()`，它的兼容性更好。


# hook 和 intercept 概述

shadowhook 支持 hook 和 intercept 两种“操作类型”：

- hook 只能作用于“函数整体”。hook 需要调用者指定一个代理函数，代理函数需要定义成和原函数同样的类型（参数类型 + 返回值类型）。当 hook 成功后，当执行到被 hook 的函数时，会先执行调用者指定的代理函数，在代理函数中，可以通过 shadowhook 的宏（或 hook 成功时返回的“原函数指针”）来调用原函数。同一个函数可以被多次 hook。
- intercept 作用于“指令地址”（这个地址可以是某个函数的首地址（函数的第一条指令的地址），也可以是函数中间某条指令的地址）。intercept 需要调用者指定一个拦截器函数。intercept 成功后，当执行到被 intercept 的指令时，会先执行调用者指定的拦截器函数（其中，输入参数中包含了所有的寄存器值），在拦截器函数中，可以读取和修改这些寄存器值。当拦截器函数返回后，会继续执行被 intercept 的指令。同一个地址可以被多次 intercept。

## hook 和 intercept 的调用关系

当函数首地址同时被 hook 和 intercept 时，会先调用所有的 intercept 拦截器函数，然后再调用 hook 代理函数。示意图如下：

![shadowhook hook and intercept](shadowhook_hook_and_intercept.png)

- 对于 hook：步骤 7 肯定会被执行。但是步骤 8 到 16 都是虚线表示，意思是：在代理函数中可以不调用“原函数”（即“链式调用关系”中的下一个代理函数。在这个调用链中，“末尾的代理函数”才是“真正的原函数”的入口地址 trampoline）。这意味着：hook 代理函数（和“真正的原函数”）都不一定会被调用。
- 对于 intercept：不存在上述 hook 的问题。各个 intercept 拦截器函数都会被调用。
- 如果不存在 intercept 拦截器函数，则上图中的调用流程 `3`，`4`，`5`，`6` 不存在，其他调用流程相同。
- 如果不存在 hook 代理函数，则调用流程稍有不同。下图描述了 intercept 的目标地址处于 callee 函数中间位置时的情况：

![shadowhook intercept instr](shadowhook_intercept_instr.png)

如果 intercept 的目标地址处于 callee 函数末尾（指执行逻辑上的末尾，就是 `RET` 指令上）会怎么样呢？调用流程是一样的，只是 trampoline 中的 branch to 没有机会被执行了，trampoline 的第一条 RET 指令执行后就已经返回到了 caller 中：

![shadowhook intercept ret](shadowhook_intercept_ret.png)

## hook 和 intercept API 概述

shadowhook 的 hook 和 intercept 一共提供了以下 API（不含 unhook 和 unintercept）：

<table>
    <tr>
        <td></td>
        <td></td>
        <td colspan="2"><strong>操作类型</strong></td>
    </tr>
    <tr>
        <td></td>
        <td></td>
        <td><strong>hook</strong></td>
        <td><strong>intercept</strong></td>
    </tr>
    <tr>
        <td rowspan="5"><strong>目标地址类型</strong></td>
        <td><strong>指令</strong></td>
        <td>暂无</td>
        <td>通过「<strong>指令地址</strong>」指定 intercept 目标<br /><code>shadowhook_intercept_instr_addr</code></td>
    </tr>
    <tr>
        <td><strong>函数</strong></td>
        <td>通过「<strong>函数地址</strong>」指定 hook 目标<br /><code>shadowhook_hook_func_addr</code><br /><code>shadowhook_hook_func_addr_2</code></td>
        <td>通过「<strong>函数地址</strong>」指定 intercept 目标<br /><code>shadowhook_intercept_func_addr</code></td>
    </tr>
    <tr>
        <td rowspan="3"><strong>有符号函数</strong></td>
        <td>通过「<strong>有符号函数的地址</strong>」指定 hook 目标<br /><code>shadowhook_hook_sym_addr</code><br /><code>shadowhook_hook_sym_addr_2</code></td>
        <td>通过「<strong>有符号函数的地址</strong>」指定 intercept 目标<br /><code>shadowhook_intercept_sym_addr</code></td>
    </tr>
    <tr>
        <td>通过「<strong>库名+函数名</strong>」指定 hook 目标<br /><code>shadowhook_hook_sym_name</code><br /><code>shadowhook_hook_sym_name_2</code></td>
        <td>通过「<strong>库名+函数名</strong>」指定 intercept 目标<br /><code>shadowhook_intercept_sym_name</code></td>
    </tr>
    <tr>
        <td>通过「<strong>库名+函数名</strong>」指定 hook 目标，hook 完成后调用 callback<br /><code>shadowhook_hook_sym_name_callback</code><br /><code>shadowhook_hook_sym_name_callback_2<code></td>
        <td>通过「<strong>库名+函数名</strong>」指定 intercept 目标，intercept 完成后调用 callback<br /><code>shadowhook_intercept_sym_name_callback</code></td>
    </tr>
</table>

### `_2` 后缀的 API

在 hook API 中：

- 不带 `_2` 后缀的 API：hook 时会使用 shadowhook 初始化时指定的“默认模式”（shared、multi、unique）。
- 带 `_2` 后缀的 API：增加了一个 `uint32_t flags` 参数，用于设置当前这个代理函数的“模式”（shared、multi、unique）。

### 目标地址类型

- **函数**：指这个地址是某个函数的第一条指令的地址。这里函数（func）的确切意思是“AAPCS64 和 AAPCS32 中的 procedure”。一般来说，程序中的其他部分会通过 `BL` / `BLR` / `BLX` 等指令来调用它们。
- **有符号函数**：指这个地址不但是个函数，它还在 ELF 的 `.dynsym` 或 `.symtab` 中有对应的符号信息。
- **指令**：指这个地址不是某个函数的第一条指令的地址，也就是说，是某个函数的中间位置的某条指令的地址。

### 当目标地址指向 thumb 指令

无论是 hook 还是 intercept。如果通过 API 指定的目标地址指向 thumb 指令，请确保地址值为奇数。shadowhook 内部是根据这个指令地址的奇偶数来判断当前是 thumb 指令还是 arm 指令。

- 通过 `shadowhook_dlsym()`, `dlsym()` 或 linker relocate 获取到的 thumb 函数地址已经是奇数了，不需要额外处理。
- 通过自己计算或内存查找获取指令地址时，需要特别注意奇偶数的问题。

### 通过库名+函数名指定 hook / intercept 目标

`shadowhook_*_sym_name*()` 系列 API 的存在目的是：

- 为了使用方便。`shadowhook_*_sym_name()` = `shadowhook_dlopen()` + `shadowhook_dlsym()` + `shadowhook_dlclose()` + `shadowhook_*_sym_addr()`。
- 当被 hook 或 intercept 的函数所在的 ELF 还没有被加载到内存时，这组 API 会在 shadowhook 内部记录这个 hook / intercept 任务，API 会返回 `errno 1 - pending task`。等对应 ELF 被加载到内存中时，shadowhook 会在 linker 调用该动态库的 `.init` 和 `.init_array` 之前，去完成之前记录的未完成的任务。如果调用的是 `shadowhook_*_sym_name_callback*()` API，在执行完 hook / intercept 任务后，shadowhook 会调用你指定的 callback。


# hook 和 unhook

> [!IMPORTANT]
> - 目前只支持针对函数整体的 hook。
> - 支持通过“函数地址”或“库名+函数名”指定 hook 目标。
> - 自动完成“新加载 ELF”的 hook（仅限“库名+函数名”方式），hook 完成后调用可选的回调函数。

## 通过“函数地址”指定 hook 目标

```C
#include "shadowhook.h"

// flags参数
#define SHADOWHOOK_HOOK_DEFAULT          0  // 0b0000  // 默认模式
#define SHADOWHOOK_HOOK_WITH_SHARED_MODE 1  // 0b0001  // shared模式
#define SHADOWHOOK_HOOK_WITH_UNIQUE_MODE 2  // 0b0010  // unique模式
#define SHADOWHOOK_HOOK_WITH_MULTI_MODE  4  // 0b0100  // multi模式
#define SHADOWHOOK_HOOK_RECORD           8  // 0b1000  // 指定目标 ELF 名称和目标地址名称

// hook 函数（使用默认模式）
void *shadowhook_hook_func_addr(void *func_addr, void *new_addr, void **orig_addr, ...);

// hook 函数（指定一个模式）
void *shadowhook_hook_func_addr_2(void *func_addr, void *new_addr, void **orig_addr, uint32_t flags, ... /* char *record_lib_name, char *record_sym_name */);
```

hook 的目标函数（`func_addr`）可以不存在于 ELF 的符号表（`.dynsym` / `.symtab`） 中。

### 参数

- `func_addr`（必须指定）：需要 hook 的函数的绝对地址。
- `new_addr`（必须指定）：新函数（代理函数）的绝对地址。
- `orig_addr`（**multi 模式：必须指定。** 其他模式：可为 `NULL`）：返回原函数地址。
- `flags`（必须指定）：附加的标识。多个标识可以在 `flags` 中进行 `|` 运算。 可以指定 hook 模式。当 `flags` 包含 `SHADOWHOOK_HOOK_RECORD` 时，`flags` 之后的两个参数用于指定“目标 ELF 名称”和“目标地址名称”，参数类型为 `char *record_lib_name, char *record_sym_name`， 这两个名称只用于写入操作记录。

### 返回值

- 非 `NULL`：hook 成功。返回值是个 stub，可保存返回值，后续用于 unhook。
- `NULL`：hook 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *orig;
void *stub;

void do_hook() {
    void *func_addr = get_hidden_func_addr();
    stub = shadowhook_hook_func_addr(func_addr, my_proxy, &orig);
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("hook failed: %d - %s", error_num, error_msg);
    }
}
```

```C
void *orig;
void *stub;

void do_hook() {
    void *func_addr = get_hidden_func_addr();
    stub = shadowhook_hook_func_addr_2(func_addr, my_proxy, &orig, SHADOWHOOK_HOOK_WITH_MULTI_MODE | SHADOWHOOK_HOOK_RECORD, "libmy.so", "my_hidden_func");
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("hook failed: %d - %s", error_num, error_msg);
    }
}
```

`func_addr` 是由一个外部函数返回的，它在 ELF 中没有符号信息。

## 通过“有符号函数的地址”指定 hook 目标

```C
#include "shadowhook.h"

// flags参数
#define SHADOWHOOK_HOOK_DEFAULT          0  // 0b0000  // 默认模式
#define SHADOWHOOK_HOOK_WITH_SHARED_MODE 1  // 0b0001  // shared模式
#define SHADOWHOOK_HOOK_WITH_UNIQUE_MODE 2  // 0b0010  // unique模式
#define SHADOWHOOK_HOOK_WITH_MULTI_MODE  4  // 0b0100  // multi模式
#define SHADOWHOOK_HOOK_RECORD           8  // 0b1000  // 指定目标 ELF 名称和目标地址名称

// hook 有符号函数（使用默认模式）
void *shadowhook_hook_sym_addr(void *sym_addr, void *new_addr, void **orig_addr);

// hook 有符号函数（指定一个模式）
void *shadowhook_hook_sym_addr_2(void *sym_addr, void *new_addr, void **orig_addr, uint32_t flags, ... /* char *record_lib_name, char *record_sym_name */);
```

`sym_addr` 必须存在于 ELF 的符号表（`.dynsym` / `.symtab`） 中。可以用 `readelf -sW` 确认。

### 参数

- `sym_addr`（必须指定）：需要 hook 的函数的绝对地址。
- `new_addr`（必须指定）：新函数（代理函数）的绝对地址。
- `orig_addr`（**multi 模式：必须指定。** 其他模式：可为 `NULL`）：返回原函数地址。
- `flags`（必须指定）：附加的标识。多个标识可以在 `flags` 中进行 `|` 运算。 可以指定 hook 模式。当 `flags` 包含 `SHADOWHOOK_HOOK_RECORD` 时，`flags` 之后的两个参数用于指定“目标 ELF 名称”和“目标地址名称”，参数类型为 `char *record_lib_name, char *record_sym_name`， 这两个名称只用于写入操作记录。

### 返回值

- 非 `NULL`：hook 成功。返回值是个 stub，可保存返回值，后续用于 unhook。
- `NULL`：hook 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *orig;
void *stub;

void do_hook() {
    stub = shadowhook_hook_sym_addr(malloc, my_proxy, &orig);
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("hook failed: %d - %s", error_num, error_msg);
    }
}
```

注意：`malloc` 的地址是在 linker relocate 时指定的。

```C
void *orig;
void *stub;

void do_hook() {
    void *handle = shadowhook_dlopen("libart.so");
    void *sym_addr = shadowhook_dlsym(handle, "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc")
    shadowhook_dlclose(handle);
    if (sym_addr == NULL) {
        LOG("symbol not found");
        return;
    }

    stub = shadowhook_hook_sym_addr_2(sym_addr, my_proxy, &orig, SHADOWHOOK_HOOK_WITH_MULTI_MODE | SHADOWHOOK_HOOK_RECORD, "libart.so", "ArtMethod::Invoke");
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("hook failed: %d - %s", error_num, error_msg);
    }
}
```

> [!TIP]
> **最佳实践**
> 
> 如果 hook 的目标 ELF 肯定已经被加载到内存中（比如 libart.so），而且需要一次 hook 目标 ELF 中的多个符号时。从性能考虑，最佳方式是：
> 1. 使用 `shadowhook_dlopen()` -> `shadowhook_dlsym()` -> `shadowhook_dlclose()` 的方式，先查询所有需要 hook 的函数的地址。
> 2. 再逐个调用 `shadowhook_hook_sym_addr_2()` 执行 hook，`flags` 中指定 `SHADOWHOOK_HOOK_RECORD` 标识，指定用于写入操作记录的“目标 ELF 名称”和“目标地址名称”。
>
> 这比直接逐个调用 `shadowhook_hook_sym_name()` 或 `shadowhook_hook_sym_name_2()` 更快，而且也能在操作记录中保存完整的目标信息。
> 
> 这样做能变快的原因是：
> 
> `shadowhook_hook_sym_name()` 和 `shadowhook_hook_sym_name_2()` 内部是通过 `shadowhook_dlopen()` -> `shadowhook_dlsym()` -> `shadowhook_dlclose()` 的方式查询符号地址的。所以：
> 
> - 多次重复调用 `shadowhook_dlopen()` 和 `shadowhook_dlclose()` 会增加耗时。
> - 如果查询的符号存在于 `.symtab` 中，查询时需要读取文件（linker 在加载 ELF 时不会映射和读取 `.symtab`）；如果符号存在于 `.symtab in .gnu_debugdata` 中，还需要执行 LZMA 解压缩。这些读取文件和解压后得到的信息，会随 `shadowhook_dlclose()` 释放。

## 通过“库名 + 函数名”指定 hook 目标

```C
#include "shadowhook.h"

// flags参数
#define SHADOWHOOK_HOOK_DEFAULT          0  // 0b0000  // 默认模式
#define SHADOWHOOK_HOOK_WITH_SHARED_MODE 1  // 0b0001  // shared 模式
#define SHADOWHOOK_HOOK_WITH_UNIQUE_MODE 2  // 0b0010  // unique 模式
#define SHADOWHOOK_HOOK_WITH_MULTI_MODE  4  // 0b0100  // multi 模式

// callback 函数定义
typedef void (*shadowhook_hooked_t)(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, void *new_addr, void *orig_addr, void *arg);

// hook 有符号函数（使用默认模式）
void *shadowhook_hook_sym_name(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr);
void *shadowhook_hook_sym_name_callback(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr, shadowhook_hooked_t hooked, void *hooked_arg);

// hook 有符号函数（指定一个模式）
void *shadowhook_hook_sym_name_2(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr, uint32_t flags);
void *shadowhook_hook_sym_name_callback_2(const char *lib_name, const char *sym_name, void *new_addr, void **orig_addr, uint32_t flags, shadowhook_hooked_t hooked, void *hooked_arg);
```

### 参数

- `lib_name`（必须指定）：符号所在 ELF 的 basename 或 pathname。对于在进程中确认唯一的动态库，可以只传 basename，例如：libart.so。对于 basename 不唯一的动态库，需要根据安卓版本和 CPU 架构自己处理兼容性，例如：`/system/lib64/libbinderthreadstate.so` 和 `/system/lib64/vndk-sp-29/libbinderthreadstate.so` 在进程中可能同时存在，遇到这种情况，如果只指定 basename，shadowhook 只会 hook 进程中第一个匹配到 basename 的动态库。
- `sym_name`（必须指定）：符号名。
- `new_addr`（必须指定）：新函数（代理函数）的绝对地址。
- `orig_addr`（**multi 模式：必须指定。** 其他模式：可为 `NULL`）：返回原函数地址。
- `flags`（必须指定）：hook 模式。
- `hooked`（可为 `NULL`）：当 hook 被执行后，调用该回调函数。回调函数的定义是 `shadowhook_hooked_t`，其中第一个参数是 hook 执行的 errno，`0` 表示成功，非 `0` 失败（可调用 `shadowhook_to_errmsg()` 获取 error message）。`shadowhook_hooked_t` 中后续参数对应 `shadowhook_hook_sym_name_callback()` 中的各个参数。
- `hooked_arg`（可为 `NULL`）：`hooked` 回调函数的最后一个参数（`arg`）的值。

### 返回值

- 非 `NULL`（**errno == 0**）：hook 成功。返回值是个 stub，可保存返回值，后续用于 unhook。
- 非 `NULL`（**errno == 1**）：由于目标 ELF 还没有加载，导致 hook 无法执行。shadowhook 内部会记录当前的 hook 任务，后续一旦目标 ELF 被加载到内存中，将立刻执行 hook 操作。返回值是个 stub，可保存返回值，后续用于 unhook。
- `NULL`：hook 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *orig;
void *stub;

void do_hook() {
    stub = shadowhook_hook_sym_name_2("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_proxy, &orig, SHADOWHOOK_HOOK_WITH_MULTI_MODE);
    int error_num = shadowhook_get_errno();
    if(stub == NULL) {
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("hook failed: %p, %d - %s", stub, error_num, error_msg);
    } else if (error_num == 1) {
        LOG("hook pending");
    }
}
```

```C
void *orig;
void *stub;

typedef void my_hooked_callback(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, void *new_addr, void *orig_addr, void *arg) {
    const char *error_msg = shadowhook_to_errmsg(error_number);
    LOG("hook finished: %s, %s, %d - %s", lib_name, sym_name, error_number, error_msg);
}

void do_hook(void) {
    stub = shadowhook_hook_sym_name_callback("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_proxy, &orig, my_hooked_callback, NULL);
    int error_num = shadowhook_get_errno();
    if(stub == NULL) {
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("hook failed: %p, %d - %s", stub, error_num, error_msg);
    } else if (error_num == 1) {
        LOG("hook pending");
    }
}
```

## unhook

```C
#include "shadowhook.h"

int shadowhook_unhook(void *stub);
```

### 参数
- `stub`（必须指定）：前述 hook 函数返回的 stub 值。

### 返回值

- `0`：unhook 成功。
- `-1`：unhook 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *stub;

void do_unhook() {
    int result = shadowhook_unhook(stub);
    if (result != 0) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("unhook failed: %d - %s", error_num, error_msg);
    }
}
```

## 代理函数

> [!IMPORTANT]
> - 代理函数编译后产物，在运行时是直接代替原函数执行的，因此，“代理函数”和“原函数”需要在运行时在CPU指令层面具有同样的“接收参数”和“传递返回值”的方式。
> - 为了实现上述的要求，最简单的方式是：把代理函数定义成和原函数同样的类型（参数个数，参数类型，返回值类型）。但这也不是绝对的，有时候原函数参数类型或返回值类型是个复杂的 C++ Class，在代理函数中也不需要读写这个 Class 的实例中的数据成员，这时也可以简化处理。具体的依据可以参考 AAPCS64 和 AAPCS32。实在没有把握或者遇到问题时，也可以反编译原函数和代理函数，从指令层面观察两个函数在处理参数和返回值时的差异。
> - 无论哪种 hook 模式，hook API 都会通过 `void **orig_addr` 参数返回原函数的地址。

### unique 模式的代理函数

> [!IMPORTANT]
> - 当需要调用原函数时，请始终通过 `orig_addr` 来完成。
> - 如果不需要调用原函数，hook 时 `void **orig_addr` 参数可以传 `NULL`。

> [!NOTE]
> **为什么不能直接通过原函数名来调用原函数？**
> 
> 结合前面 hook 和 intercept 的调用关系图：因为 inline hook 会修改原函数（callee）的头部指令（`instruction 1`），改为跳转到代理函数地址的指令（`branch to`）。如果在代理函数中继续使用“原函数名”来调用原函数，就会导致代理函数被递归执行。hook 完成后，`orig_addr` 其实是一个指向 trampoline 的指针。

#### 举例

```C
void *orig; //全局变量
void *stub;

typedef void *(*malloc_t)(size_t);

void *my_malloc(size_t sz) {
    if(sz > 1024)
        return nullptr;

    // 调用原函数
    return ((malloc_t)orig)(sz);
}

void do_hook(void) {
    stub = shadowhook_hook_sym_addr_2(malloc, my_malloc, &orig, SHADOWHOOK_HOOK_WITH_UNIQUE_MODE);
}

void do_unhook(void) {
    shadowhook_unhook(stub);
}
```

### multi 模式的代理函数

> [!IMPORTANT]
> - 当需要调用原函数时，请始终通过 `orig_addr` 来完成。
> - 无论是否需要调用原函数，hook 时 `void **orig_addr` 参数都不可为 `NULL`，并且用于保存 `orig_addr` 的内存必须始终可读写访问（例如：全局变量，`static` 变量，或包含 `orig_addr` 的结构体或类的实例是始终存在的）。

> [!NOTE]
> **为什么 multi 模式中 `void **orig_addr` 参数不可为 `NULL` ？**
> 
> 在 multi 模式中，一个函数可以被多次 hook。如果所有代理函数中都通过 `orig_addr` 调用原函数，则会形成如下图所示的链式调用关系。这时，只有链表末尾的 `orig_addr` 是指向 trampoline 的，之后它会调用真正的原函数，其他节点的 `orig_addr` 都是指向“链表中下一个节点的代理函数”。
> 
> 如果在下图中想要 unhook `proxy 2`，shadowhook 后修改 `orig_addr 1` 的值让它指向 `proxy 3` 代理函数。**是的，调用者的 `orig_addr` 中的值在 hook 完成之后，shadowhook 可能还会再次修改它。**
> 
> 因此，`orig_addr` 不仅仅被用于“在代理函数中调用原函数”，它也被 shadowhook 用于维护 multi 模式中的代理函数的链式调用关系。
> 
> **对于使用者来说，不要自己随意修改 `orig_addr` 的值，即使是 unhook 后，也不要将 orig_addr 的值设置为 `NULL`，这是因为：在多线程环境中，你在 unhook 的同时，另一个线程可能正在执行“正在被你 unhook 的代理函数”，在下一个瞬间，代理函数中可能需要通过 `orig_addr` 来获取“代理函数链表中下一个代理函数的地址”。**

![shadowhook multi mode](shadowhook_multi_mode.png)

#### 举例

```C
void *orig; //全局变量
void *stub;

typedef void *(*malloc_t)(size_t);

void *my_malloc(size_t sz) {
    if(sz > 1024)
        return nullptr;

    // 调用原函数
    return ((malloc_t)orig)(sz);
}

void do_hook(void) {
    // orig_addr不可为NULL
    stub = shadowhook_hook_sym_addr_2(malloc, my_malloc, &orig, SHADOWHOOK_HOOK_WITH_MULTI_MODE);
}

void do_unhook(void) {
    shadowhook_unhook(stub);
    stub = NULL;
}
```

### shared 模式的代理函数

> [!IMPORTANT]
> - 当需要调用原函数时，请始终使用 `SHADOWHOOK_CALL_PREV` 宏来完成。
> - 请始终添加 `SHADOWHOOK_POP_STACK` 宏或 `SHADOWHOOK_STACK_SCOPE` 宏来完成额外的清理逻辑。

> [!NOTE]
> **为什么 shared 模式中不直接用 `orig_addr` 调用原函数？而是如此麻烦？**
>
> 在 shared 模式中，hook API 通过 `orig_addr` 返回的地址，总是指向“真正的原函数”的。如果直接通过 `orig_addr` 调用原函数，则会跳后“代理函数链表中后续所有的代理函数”。

![shadowhook shared mode](shadowhook_shared_mode.png)

#### `SHADOWHOOK_CALL_PREV` 宏

```C
#include "shadowhook.h"

#ifdef __cplusplus
#define SHADOWHOOK_CALL_PREV(func, ...) ...
#else
#define SHADOWHOOK_CALL_PREV(func, func_sig, ...) ...
#endif
```

- 用于在代理函数中调用原函数。在代理函数中也可以不调用原函数，但请不要通过函数名或 `orig_addr` 来直接调用原函数。
- 在 C++ 源文件中的用法是：第一个参数传递当前的代理函数的地址，后面按照顺序依次传递函数的各个参数。
- 在 C 源文件中的用法是：第一个参数传递当前的代理函数的地址，第二个参数传递当前 hook 的函数的类型定义，后面按照顺序依次传递函数的各个参数。

#### `SHADOWHOOK_POP_STACK` 宏和 `SHADOWHOOK_STACK_SCOPE` 宏

```C
#include "shadowhook.h"

// pop stack in proxy-function (for C/C++)
#define SHADOWHOOK_POP_STACK() ...

// pop stack in proxy-function (for C++ only)
#define SHADOWHOOK_STACK_SCOPE() ...
```

shared 模式需要在代理函数中做一些额外的事情：“执行 shadowhook 内部的 stack 清理”，这需要你手动调用 `SHADOWHOOK_POP_STACK` 宏或 `SHADOWHOOK_STACK_SCOPE` 宏来完成（二选一）。

**注意：即使你在代理函数中什么也不做，也不调用原函数，这时，也需要调用上述的宏做 stack 清理。**

- `SHADOWHOOK_POP_STACK` 宏：适用于 C 和 C++ 源文件。需要确保在代理函数返回前调用一次。
- `SHADOWHOOK_STACK_SCOPE` 宏：适用于 C++ 源文件。在函数开头调用一次即可。

> [!NOTE]
> **不调用 `SHADOWHOOK_POP_STACK` 宏或 `SHADOWHOOK_STACK_SCOPE` 宏会怎么样？**
> 
> 这会导致：当执行到被 hook 函数时，代理函数只会被执行一次，下次再执行到被 hook 函数时，由于 hub 模块发现“当前 hook 点的代理函数调用状态”还在，于是判定会发生环形递归调用，这时 hub 模块不再会调用这个 hook 点的任何代理函数，而是会直接调用“真正的原函数”。

> [!NOTE]
> **shadowhook 为什么不自动完成 `SHADOWHOOK_POP_STACK` 宏和 `SHADOWHOOK_STACK_SCOPE` 宏的功能？而是要使用者在代理函数中手动调用？将复杂度暴露给使用者？**
> 
> 概括来说，上图中 shadowhook hub 模块中的 enter 是一段汇编写的跳板逻辑，在这段逻辑中，在调用代理函数时，需要保持 `SP` 不变，否则可能会导致代理函数中无法获取到正确的输入参数，这肯定不行，但是如果要保持 `SP` 不变，就无法在调用代理函数之前在栈上保存 `LR`。这会导致：在代理函数中执行“基于 CFI 的栈回溯”时，回溯到 enter 就终止了，这对于某些 hook 使用场景来说是不可接受的，而且 CFI 栈回溯失败也会影响到 native 崩溃时 backtrace 获取。那怎么办呢？目前的做法是：enter 逻辑中不在栈上保存 `LR` 了，但是将“调用代理函数”的操作改成 tail call，这样一来，在代理函数中执行 CFI 栈回溯时，就能跳过 enter 继续回溯，但是代价是：我们失去了“在所有代理函数执行完之后，执行当前 hook 点的代理函数调用状态清除”的时机，这会导致前面提到的“代理函数只会被执行一次的问题”。于是，最终的妥协方案是：将“执行当前 hook 点的代理函数调用状态清除”逻辑封装成一个宏，放在代理函数中去执行。这是多种因素最终导致的结果。
> 
> 这是一个比较复杂的问题，可能需要结合 shadowhook 源码，经过自己的调试分析后，才能彻底理解。

#### SHADOWHOOK_RETURN_ADDRESS 宏

```C
#include "shadowhook.h"

// get return address in proxy-function
#define SHADOWHOOK_RETURN_ADDRESS() ...
```

偶尔，你可能需要在代理函数中通过 `__builtin_return_address(0)` 获取 `LR` 的值，由于 shared 模式中 `enter` 改变了 `LR`，直接调用 `__builtin_return_address(0)` 将返回 `enter` 中的地址。

在代理函数中，需要通过 `SHADOWHOOK_RETURN_ADDRESS` 宏来获取原始的 `LR` 值。

#### `SHADOWHOOK_ALLOW_REENTRANT` 宏和 `SHADOWHOOK_DISALLOW_REENTRANT` 宏

```C
#include "shadowhook.h"

// allow reentrant of the current proxy-function
#define SHADOWHOOK_ALLOW_REENTRANT() ...

// disallow reentrant of the current proxy-function
#define SHADOWHOOK_DISALLOW_REENTRANT() ...
```

在 shared 模式中，默认是不允许代理函数被重入的（递归调用）。但是，某些特殊使用场景中，由业务逻辑控制的重入可能是需要的，他们并不会形成“无限的”调用环，而是会在某些业务条件满足时终止。如果你确认你的使用场景是这种情况，请在代理函数中调用 `SHADOWHOOK_ALLOW_REENTRANT` 以允许重入，当代理函数的逻辑运行到“不再需要允许重入的部分”时，可以调用 `SHADOWHOOK_DISALLOW_REENTRANT` 不再允许重入。

#### 举例一（C 源文件）

```C
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void *malloc_proxy(size_t sz) {
    if(sz > 1024) {
        // 执行 stack 清理（不可省略）。即使不调用原函数，也不可省略！
        SHADOWHOOK_POP_STACK();
        return NULL;
    }

    // 调用原函数
    void *result = SHADOWHOOK_CALL_PREV(malloc_proxy, malloc_t, sz);

    // 执行 stack 清理（不可省略）
    SHADOWHOOK_POP_STACK();
    return result;
} 

void do_hook(void) {
    stub = shadowhook_hook_sym_addr_2(malloc, malloc_proxy, &orig, SHADOWHOOK_HOOK_WITH_SHARED_MODE);
}

void do_unhook(void) {
    shadowhook_unhook(stub);
    stub = NULL;
}

void *my_malloc_4k(void) {
    // 在某些场景中，也许你需要直接调用原函数
    return ((malloc_t)orig)(4096);
}
```

#### 举例二（C++ 源文件）

```C++
void *orig;
void *stub;

typedef void *(*malloc_t)(size_t);

void * malloc_proxy(size_t sz) {
    // 执行 stack 清理（不可省略），只需调用一次
    SHADOWHOOK_STACK_SCOPE();
    
    if(sz > 1024)
        return nullptr;

    // 调用原函数
    return SHADOWHOOK_CALL_PREV(malloc_proxy, sz);
} 

void do_hook(void) {
    stub = shadowhook_hook_sym_addr_2(malloc, malloc_proxy, &orig, SHADOWHOOK_HOOK_WITH_SHARED_MODE);
}

void do_unhook(void) {
    shadowhook_unhook(stub);
    stub = NULL;
} 

void *my_malloc_4k(void) {
    // 在某些场景中，也许你需要直接调用原函数
    return ((malloc_t)orig)(4096);
}
```

#### 举例三（控制代理函数是否可重入）

```C
int test_func_2(int a, int b) {
    a--; //a每次递减1
    return test_func_1(a, b);
}

int test_func_1(int a, int b) {
    if(a < b)
        return 0;
    else
        return test_func_2(a, b);
}

void test(void) {
    test_func_1(10, 5);
}
```

`test_func_1` 和 `test_func_2` 看似会形成一个无限循环的环形调用，但是当 `a < b` 时，循环会终止，所以并不会真的死循环。（`test` 函数中调用 `test_func_1` 的参数是`a=10` 和 `b=5`，每次 `test_func_2` 中将 `a` 递减 `1`）

默认情况下 shadowhook 会阻止代理函数的重入，因为重入很容易导致代理函数之间形成死循环。但如果这种代理函数的重入正是你所需要的，请参考下面的例子用 `SHADOWHOOK_ALLOW_REENTRANT` 宏和 `SHADOWHOOK_DISALLOW_REENTRANT` 宏来控制代理函数中某个代码区域的“可重入性”：

下面的代码 hook `test_func_1`，其中使用 `SHADOWHOOK_ALLOW_REENTRANT` 宏来允许重入。

```C++
void *stub;

int test_func_1_proxy(int a, int b) {
    // 执行 stack 清理（不可省略），只需调用一次
    SHADOWHOOK_STACK_SCOPE();
    
    // 加点自己的业务逻辑
    if(a > 1024 || b > 1024)
        return -1;

    // 下面要开始调用原函数了，我们希望每次对 test_func_1 的调用都走入我们的代理函数中
    SHADOWHOOK_ALLOW_REENTRANT();

    // 调用原函数
    int result = SHADOWHOOK_CALL_PREV(test_func_1_proxy, sz);
    
    // 恢复 shadowhook 的“防止代理函数被重入”的保护功能
    SHADOWHOOK_DISALLOW_REENTRANT();
    
    // 继续加点业务逻辑
    write_log(global_log_fd, "test_func_1 called with a=%d, b=%d", a, b);
    
    return result;
} 

void do_hook(void) {
    stub = shadowhook_hook_sym_addr_2(test_func_1, test_func_1_proxy, NULL, SHADOWHOOK_HOOK_WITH_SHARED_MODE);
}

void do_unhook(void) {
    shadowhook_unhook(stub);
    stub = NULL;
}
```


# intercept 和 unintercept

intercept 作用于“指令地址”（可以是某个函数的首地址（函数的第一条指令的地址），也可以是函数中间某条指令的地址）。intercept 需要调用者指定一个拦截器函数。intercept 成功后，当执行到被 intercept 的指令时，会先执行调用者指定的拦截器函数（其中，输入参数中包含了所有的寄存器值），在拦截器函数中，可以读取和修改这些寄存器值。当拦截器函数返回后，会继续执行被 intercept 的指令。

> [!IMPORTANT]
> - intercept 与 hook 模式无关，无论何种“hook 模式”，同一个地址上都可以添加多个拦截器，运行时将依次执行。
> - 在拦截器函数中：所有的寄存器值都可以读取。除了 `SP` 和 `PC` 以外，其他寄存器值都可以修改（`SP` 和 `PC` 修改无效）。
> - 由于 fpsimd 寄存器的读写指令执行都比较耗时，所以在 intercept API 中提供了 `flags` 控制是否需要“读取”和“修改” fpsimd 寄存器。如果不需要访问 fpsimd 寄存器，可以使用 DEFAULT `flags`，此时 fpsimd 相关寄存器值为随机值，修改操作也无效。

```C
// fpsimd 寄存器
typedef union {
#if defined(__aarch64__)
  __uint128_t q;
#endif
  uint64_t d[2];
  uint32_t s[4];
  uint16_t h[8];
  uint8_t b[16];
} shadowhook_vreg_t;

#if defined(__aarch64__)
// arm64 寄存器
typedef struct {
  uint64_t regs[31];  // x0-x30
  uint64_t sp;
  uint64_t pc;
  uint64_t pstate;
  shadowhook_vreg_t vregs[32];  // q0-q31
  uint64_t fpsr;
  uint64_t fpcr;
} shadowhook_cpu_context_t;
#elif defined(__arm__)
// arm32 寄存器
typedef struct {
  uint32_t regs[16];  // r0-r15
  uint32_t cpsr;
  uint32_t fpscr;
  // (1) NEON, VFPv4, VFPv3-D32: q0-q15(d0-d31)
  // (2) VFPv3-D16: q0-q7(d0-d15)
  shadowhook_vreg_t vregs[16];
} shadowhook_cpu_context_t;
#endif

// 拦截器函数的定义
typedef void (*shadowhook_interceptor_t)(shadowhook_cpu_context_t *cpu_context, void *data);

// flags 参数
//
// 只读写通用寄存器（读写 fpsimd 寄存器比较耗时，不需要的情况下建议使用 DEFAULT）
#define SHADOWHOOK_INTERCEPT_DEFAULT                0
// 在 DEFAULT 基础上增加读取 fpsimd 寄存器
#define SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_ONLY  1
// 在 DEFAULT 基础上增加写入 fpsimd 寄存器
#define SHADOWHOOK_INTERCEPT_WITH_FPSIMD_WRITE_ONLY 2
// 在 DEFAULT 基础上增加读取和写入 fpsimd 寄存器
#define SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_WRITE 3
// 指定目标 ELF 名称和目标地址名称
#define SHADOWHOOK_INTERCEPT_RECORD                 4
```

## 通过“指令地址”指定 intercept 目标

```C
#include "shadowhook.h"

void *shadowhook_intercept_instr_addr(void *instr_addr, shadowhook_interceptor_t pre, void *data, uint32_t flags, ... /* char *record_lib_name, char *record_sym_name */);
```

### 参数

- `instr_addr`（必须指定）：需要 intercept 的指令的绝对地址。
- `pre`（必须指定）：拦截器函数的绝对地址。
- `data`（可为 `NULL`）：`pre` 拦截器函数的最后一个参数（`data`）的值。
- `flags`（必须指定）：附加的标识。多个标识可以在 `flags` 中进行 `|` 运算。可以指定需要在拦截器函数中读写哪些寄存器。当 `flags` 包含 `SHADOWHOOK_INTERCEPT_RECORD` 时，`flags` 之后的两个参数用于指定“目标 ELF 名称”和“目标地址名称”，参数类型为 `char *record_lib_name, char *record_sym_name`，这两个名称只用于写入操作记录。

### 返回值

- 非 `NULL`：intercept 成功。返回值是个 stub，可保存返回值，后续用于 unintercept。
- `NULL`：intercept 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

我们需要在 libc 的 `read()` 函数的某条指令处添加一个拦截器，然后执行一些逻辑，由于涉及到寄存器操作，一般来说 arm 和 arm64 需要分开定义：

```C
void *stub;

#if defined(__arm__)
void my_interceptor_arm(shadowhook_cpu_context_t *cpu_context, void *data) {
    if (cpu_context->regs[5] == 0) { // 当 r5 等于 0 时，修改 r6 和 r7 的值
        cpu_context->regs[6] = 1;
        cpu_context->regs[7] = 10;
        LOG("arm: found r5 == 0, data = %p", data);
    }
}

void do_intercept(void) {
    // 目标地址在 read 函数的 offset+8 处，该处是一条 thumb 指令，确保传入的地址值是奇数
    void *instr_addr = (read + 8) & 0x1; 
    stub = shadowhook_intercept_instr_addr(instr_addr, my_interceptor_arm, 0x123, SHADOWHOOK_INTERCEPT_RECORD, "libc.so", "read+8");
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("intercept failed: %p, %d - %s", stub, error_num, error_msg);
    }
}
#endif

#if defined(__aarch64__)
void my_interceptor_arm64(shadowhook_cpu_context_t *cpu_context, void *data) {
    if (cpu_context->regs[19] == 0) { // 当 x19 等于 0 时，修改 x20 和 x21 的值
        cpu_context->regs[20] = 1;
        cpu_context->regs[21] = 1000;
        LOG("arm64: found x19 == 0, data = %p", data);
    }
}

void do_intercept(void) {
    // 目标地址在 read 函数的 offset 20 处，由于 arm64 是 4 字节定长指令，相当于 interceptor 添加在 read 函数的“第 5 条指令”和“第 6 条指令”之间，interceptor 执行完之后，再执行第 6 条指令
    void *instr_addr = read + 20;
    stub = shadowhook_intercept_instr_addr(instr_addr, my_interceptor_arm64, 0x123, SHADOWHOOK_INTERCEPT_RECORD, "libc.so", "read+20");
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("intercept failed: %p, %d - %s", stub, error_num, error_msg);
    }
}
#endif
```

例子中直接写 `read` 可以获取到 `read` 函数运行时的绝对地址，地址查找和保存是由 linker 在 relocate 阶段完成的。这种写法比较方便，但是容易被 PLT hook，请酌情考虑。

## 通过“函数地址”指定 intercept 目标

```C
#include "shadowhook.h"

void *shadowhook_intercept_func_addr(void *func_addr, shadowhook_interceptor_t pre, void *data, uint32_t flags, ... /* char *record_lib_name, char *record_sym_name */);
```

### 参数

- `func_addr`（必须指定）：需要 intercept 的函数的绝对地址。
- `pre`（必须指定）：拦截器函数的绝对地址。
- `data`（可为 `NULL`）：`pre` 拦截器函数的最后一个参数（`data`）的值。
- `flags`（必须指定）：附加的标识。多个标识可以在 `flags` 中进行 `|` 运算。可以指定需要在拦截器函数中读写哪些寄存器。当 `flags` 包含 `SHADOWHOOK_INTERCEPT_RECORD` 时，`flags` 之后的两个参数用于指定“目标 ELF 名称”和“目标地址名称”，参数类型为 `char *record_lib_name, char *record_sym_name`，这两个名称只用于写入操作记录。

### 返回值

- 非 `NULL`：intercept 成功。返回值是个 stub，可保存返回值，后续用于 unintercept。
- `NULL`：intercept 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *stub;

#if defined(__aarch64__)
void my_interceptor_arm64(shadowhook_cpu_context_t *cpu_context, void *data) {
    if (cpu_context->regs[19] == 0) { // 当 x19 等于 0 时，修改 x20 和 x21 的值
        cpu_context->regs[20] = 1;
        cpu_context->regs[21] = 1000;
        LOG("arm64: found x19 == 0, data = %p", data);
    }
}

void do_intercept(void) {
    void *func_addr = get_hidden_func_addr();
    stub = shadowhook_intercept_func_addr(func_addr, my_interceptor_arm64, 0x123, SHADOWHOOK_INTERCEPT_RECORD, "libmy.so", "my_hidden_func");
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("intercept failed: %p, %d - %s", stub, error_num, error_msg);
    }
}
#endif
```

## 通过“有符号函数地址”指定 intercept 目标

```C
#include "shadowhook.h"

void *shadowhook_intercept_sym_addr(void *sym_addr, shadowhook_interceptor_t pre, void *data, uint32_t flags, ... /* char *record_lib_name, char *record_sym_name */);
```

### 参数

- `sym_addr`（必须指定）：需要 intercept 的函数的绝对地址。
- `pre`（必须指定）：拦截器函数的绝对地址。
- `data`（可为 `NULL`）：`pre`拦截器函数的最后一个参数（`data`）的值。
- `flags`（必须指定）：附加的标识。多个标识可以在 `flags` 中进行 `|` 运算。可以指定需要在拦截器函数中读写哪些寄存器。当 `flags` 包含 `SHADOWHOOK_INTERCEPT_RECORD` 时，`flags` 之后的两个参数用于指定“目标 ELF 名称”和“目标地址名称”，参数类型为 `char *record_lib_name, char *record_sym_name`，这两个名称只用于写入操作记录。

### 返回值

- 非 `NULL`：intercept 成功。返回值是个 stub，可保存返回值，后续用于 unintercept。
- `NULL`：intercept 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *stub;

#if defined(__aarch64__)
void my_interceptor_arm64(shadowhook_cpu_context_t *cpu_context, void *data) {
    if (cpu_context->regs[19] == 0) { // 当 x19 等于0时，修改 x20 和 x21 的值
        cpu_context->regs[20] = 1;
        cpu_context->regs[21] = 1000;
        LOG("arm64: found x19 == 0, data = %p", data);
    }
}

void do_intercept(void) {
    void *handle = shadowhook_dlopen("libart.so");
    void *sym_addr = shadowhook_dlsym(handle, "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc");
    shadowhook_dlclose(handle);
    if (sym_addr == NULL) {
        LOG("symbol not found");
        return;
    }
    
    stub = shadowhook_intercept_sym_addr(sym_addr, my_interceptor_arm64, 0x123, SHADOWHOOK_INTERCEPT_RECORD, "libart.so", "ArtMethod::Invoke");
    if(stub == NULL) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("intercept failed: %p, %d - %s", stub, error_num, error_msg);
    }
}
#endif
```

## 通过“库名 + 函数名”指定 intercept 目标

```C
#include "shadowhook.h"

// callback函数定义
typedef void (*shadowhook_intercepted_t)(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, shadowhook_interceptor_t pre, void *data, void *arg);
                                         
void *shadowhook_intercept_sym_name(const char *lib_name, const char *sym_name, shadowhook_interceptor_t pre, void *data, uint32_t flags);
void *shadowhook_intercept_sym_name_callback(const char *lib_name, const char *sym_name, shadowhook_interceptor_t pre, void *data, uint32_t flags, shadowhook_intercepted_t intercepted, void *intercepted_arg);
```

### 参数

- `lib_name`（必须指定）：符号所在 ELF 的 basename 或 pathname。对于在进程中确认唯一的动态库，可以只传basename，例如：libart.so。对于 basename 不唯一的动态库，需要根据安卓版本和 arch 自己处理兼容性，例如：`/system/lib64/libbinderthreadstate.so` 和 `/system/lib64/vndk-sp-29/libbinderthreadstate.so` 在进程中可能同时存在，遇到这种情况，如果只指定 basename 的话，shadowhook 只会 intercept 进程中第一个匹配到 basename 的动态库。
- `sym_name`（必须指定）：符号名。
- `pre`（必须指定）：拦截器函数的绝对地址。
- `data`（可为 `NULL`）：`pre`拦截器函数的最后一个参数（`data`）的值。
- `flags`（必须指定）：附加的标识。多个标识可以在 `flags` 中进行 `|` 运算。可以指定需要在拦截器函数中读写哪些寄存器。
- `intercepted`（可为 `NULL`）：当 intercept 被执行后，调用该回调函数。回调函数的定义是 `shadowhook_intercepted_t`，其中第一个参数是 intercept 执行的 errno，`0` 表示成功，非 `0` 失败（可调用 `shadowhook_to_errmsg()` 获取 error message）。`shadowhook_intercepted_t` 中后续参数对应 `shadowhook_intercept_sym_name_callback` 中的各个参数。
- `intercepted_arg`（可为 `NULL`）：intercepted 回调函数的最后一个参数（`arg`）的值。

### 返回值

- 非 `NULL`（**errno == 0**）：intercept 成功。返回值是个 stub，可保存返回值，后续用于 unintercept。
- 非 `NULL`（**errno == 1**）：由于目标动态库还没有加载，导致 intercept 无法执行。shadowhook 内部会记录当前的 intercept 任务，后续一旦目标动态库被加载到内存中，将立刻执行 intercept 操作。返回值是个 stub，可保存返回值，后续用于 unintercept。
- `NULL`：intercept 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *stub;

#if defined(__aarch64__)
void my_interceptor_arm64(shadowhook_cpu_context_t *cpu_context, void *data) {
    if (cpu_context->regs[19] == 0) { // 当 x19 等于 0 时，修改 x20 和 x21 的值
        cpu_context->regs[20] = 1;
        cpu_context->regs[21] = 1000;
        LOG("arm64: found x19 == 0, data = %p", data);
    }
}

void do_intercept(void) {
    stub = shadowhook_intercept_sym_name("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_interceptor_arm64, 0x123, SHADOWHOOK_INTERCEPT_DEFAULT);
    int error_num = shadowhook_get_errno();
    if(stub == NULL) {
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("intercept failed: %p, %d - %s", stub, error_num, error_msg);
    } else if (error_num == 1) {
        LOG("intercept pending");
    }
}
#endif
```

```C
void *stub;

typedef void my_intercepted_callback(int error_number, const char *lib_name, const char *sym_name, void *sym_addr, shadowhook_interceptor_t pre, void *data, void *arg) {
    const char *error_msg = shadowhook_to_errmsg(error_number);
    LOG("hook finished: %s, %s, %d - %s", lib_name, sym_name, error_number, error_msg);
}

#if defined(__aarch64__)
void my_interceptor_arm64(shadowhook_cpu_context_t *cpu_context, void *data) {
    if (cpu_context->regs[19] == 0) { // 当 x19 等于 0 时，修改 x20 和 x21 的值
        cpu_context->regs[20] = 1;
        cpu_context->regs[21] = 1000;
        LOG("arm64: found x19 == 0, data = %p", data);
    }
}

void do_intercept(void) {
    stub = shadowhook_intercept_sym_name_callback("libart.so", "_ZN3art9ArtMethod6InvokeEPNS_6ThreadEPjjPNS_6JValueEPKc", my_interceptor_arm64, 0x123, SHADOWHOOK_INTERCEPT_DEFAULT, my_intercepted_callback, NULL);
    int error_num = shadowhook_get_errno();
    if(stub == NULL) {
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("intercept failed: %p, %d - %s", stub, error_num, error_msg);
    } else if (error_num == 1) {
        LOG("intercept pending");
    }
}
#endif
```

## unintercept

```C
#include "shadowhook.h"

int shadowhook_unintercept(void *stub);
```

### 参数
- `stub`（必须指定）：前述 intercept 函数返回的 stub 值。

### 返回值

- `0`：unintercept 成功。
- `-1`：unintercept 失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

### 举例

```C
void *stub;

void do_unintercept() {
    int result = shadowhook_unintercept(stub);
    if (result != 0) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("unintercept failed: %d - %s", error_num, error_msg);
    }
}
```


# 监控 linker load 和 unload ELF

> [!IMPORTANT]
> shadowhook 只在 Android 5.0 及以上操作系统版本中支持该功能。

Android linker 加载（load）ELF 的最后一步是调用 `soinfo::call_constructors()` 函数，其中会执行 ELF 中 `.init` section 和 `.init_array` section 中所包含的函数；在卸载（unload）ELF 时，会先执行 `soinfo::call_destructors()` 函数，其中会执行 ELF 中 `.fini` section和 `.fini_array` section 中所包含的函数。这是两个很重要的时机，例如：可以在执行 `soinfo::call_constructors()` 之前对 ELF 执行 PLT hook，JNI hook，或者直接修改指令等。

shadowhook 提供了一组 API，用于监控 linker load 和 unload ELF 的行为。这组 API 可以注册 ELF 的 `.init` + `.init_array` 和 `.fini` + `.fini_array` 被执行前后的回调函数。回调函数的参数和 `dl_iterate_phdr()` 完全相同：`typedef void (*)(struct dl_phdr_info *info, size_t size, void *data);`。

> [!NOTE]
> **直接让使用者 hook `soinfo::call_constructors()` 和 `soinfo::call_destructors()` 不就行了？为什么要增加这组API？**
> 
> 增加这组 API 的原因是：
> 
> - linker 调用 `soinfo::call_constructors()` 不一定会真的执行 `.init` 和 `.init_array`，你在代码中调用 `dlopen("libc.so");` 也会导致 linker 对 `libc.so` 的 `soinfo` 调用 `soinfo::call_constructors()`。在 `soinfo::call_constructors()` 内部，还会通过 flag 来判断是否已经调用过了，以确保 `.init` 和 `.init_array` 只被执行一次。shadowhook 通过内存扫描来确定这个 flag 字段相对于 `soinfo` 的 offset。回调函数中的 `struct dl_phdr_info *info` 会包含 ELF 的基本信息，shadowhook 也需要通过内存扫描获取这些信息相对于 `soinfo` 的 offset。这些扫描逻辑比较复杂，而且需要做 Android OS 版本兼容性处理。所以并不是简单的 hook 这两个函数就可以了。
> - 这组功能具有通用性，在一些已知的场景中需要使用。
> - shadowhook 自身也需要使用这项功能（当 hook 或 intercept 一个还未加载到内存中的 ELF 时，shadowhook 会记录这个任务，之后 linker 加载这个 ELF 时，在执行 `.init` 和 `.init_array` 之前，需要完成 hook 或 intercept 任务）

```C
#include "shadowhook.h"

// 回调函数定义
typedef void (*shadowhook_dl_info_t)(struct dl_phdr_info *info, size_t size, void *data);

// 注册和反注册 .init + .init_array 的回调函数
int shadowhook_register_dl_init_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int shadowhook_unregister_dl_init_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);

// 注册和反注册 .fini + .fini_array 的回调函数
int shadowhook_register_dl_fini_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int shadowhook_unregister_dl_fini_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
```

## 参数

- `pre`（可为 `NULL`）：执行 `.init` + `.init_array` 或 `.fini` + `.fini_array` 之前的回调函数。
- `post`（可为 `NULL`）：执行 `.init` + `.init_array` 或 `.fini` + `.fini_array` 之后的回调函数。
- `data`（可为 `NULL`）：`pre` 和 `post` 回调函数的最后一个参数（`data`）的值。

**注意：`pre` 和 `post` 不可同时为 `NULL`。**

## 返回值

- `0`：成功。
- `-1`：失败。可调用 `shadowhook_get_errno()` 获取 errno，可继续调用 `shadowhook_to_errmsg()` 获取 error message。

## 举例

```C
#include "shadowhook.h"

static void dl_init_pre(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size, (void)data;
  LOG("dl_init_pre. load_bias %" PRIxPTR ", %s", (uintptr_t)info->dlpi_addr, info->dlpi_name);
}

static void dl_fini_post(struct dl_phdr_info *info, size_t size, void *data) {
  (void)size, (void)data;
  LOG("dl_fini_post. load_bias %" PRIxPTR ", %s", (uintptr_t)info->dlpi_addr, info->dlpi_name);
}

void register_callback(void) {
    if (0 != shadowhook_register_dl_init_callback(dl_init_pre, NULL, NULL)) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("register dl_init callback. failed: %d - %s", error_num, error_msg);
    }

    if (0 != shadowhook_register_dl_fini_callback(NULL, dl_fini_post, NULL)) {
        int error_num = shadowhook_get_errno();
        const char *error_msg = shadowhook_to_errmsg(error_num);
        LOG("register dl_fini callback. failed: %d - %s", error_num, error_msg);
    }
}
```


# 错误码

| error number | error message | 描述 |
| :---- | :---- | :---- |
| 0 | OK | 成功 |
| 1 | Pending task | “通过库名 + 函数名执行的 hook / intercept”时由于所属 ELF 未加载而无法完成，当前 hook / intercept 任务处于 pending 状态 |
| 2 | Not initialized | shadowhook 未初始化 |
| 3 | Invalid argument | API 输入参数非法 |
| 4 | Out of memory | 内存不足 |
| 5 | MProtect failed | `mprotect()` 执行失败 |
| 6 | Write to arbitrary address crashed | 向一个绝对地址写入指令数据时，由于发生 `SIGSEGV` 或 `SIGBUS` 而导致写入失败 |
| 7 | Init errno mod failed | errno 模块初始化失败 |
| 8 | Init bytesig mod SIGSEGV failed | bytesig 模块的 `SIGSEGV` 保护机制初始化失败 |
| 9 | Init bytesig mod SIGBUS failed | bytesig 模块的 `SIGBUS` 保护机制初始化失败 |
| 10 | Duplicate intercept | intercept 时，发现在同一个目标地址上，已经存在了一个 `pre` 和 `data` 参数都相同的拦截器 |
| 11 | Init safe mod failed | safe 模块初始化失败 |
| 12 | Init linker mod failed | linker 模块初始化失败 |
| 13 | Init hub mod failed| hub 模块初始化失败 |
| 14 | Create hub failed | hook 时，为 shared 模式创建 hub 失败 |
| 15 | Monitor dlopen failed | 监控 `dlopen()` 失败（只可能会发生在 Android 4.x 中） |
| 16 | Duplicate shared hook | hook 时，发现在同一个目标地址上，在 shared 模式的 hub 中已经存在了一个相同地址的代理函数 |
| 17 | Open ELF crashed | hook 或 intercept 时，通过库名和符号名查找目标地址时，在查找目标 ELF 时，由于发生 `SIGSEGV` 或 `SIGBUS` 而导致失败（只可能会发生在 Android 4.x 中） |
| 18 | Find symbol in ELF failed | hook 或 intercept 时，通过库名和符号名查找目标地址时，在目标 ELF 中未找到该符号 |
| 19 | Find symbol in ELF crashed | hook 或 intercept 时，通过库名和符号名查找目标地址时，在查找符号时，由于发生 `SIGSEGV` 或 `SIGBUS` 而导致失败 |
| 20 | Duplicate unique hook | hook 时，发现在同一个目标地址上，已经存在了一个 unique 模式的代理函数 |
| 21 | Dladdr crashed | hook 或 intercept 时，通过目标地址获取符号信息时，由于发生 `SIGSEGV` 或 `SIGBUS` 而导致失败（只可能会发生在 Android 4.x 中） |
| 22 | Find dlinfo failed | hook 或 intercept 时，通过目标地址获取符号信息时，未找到符号信息 |
| 23 | Symbol size too small | hook 或 intercept 时，由于目标地址所在函数的后续指令长度不足，导致失败 |
| 24 | Alloc enter failed | hook 或 intercept 时，分配入口跳板失败 |
| 25 | Instruction rewrite crashed | hook 或 intercept 时，执行指令重写时，由于发生 `SIGSEGV` 或 `SIGBUS` 而导致失败 |
| 26 | Instruction rewrite failed | hook 或 intercept 时，指令重写失败 |
| 27 | Unop not found | unhook 或 unintercept 时，未找到该代理函数或拦截器函数 |
| 28 | Verify original instruction crashed | unhook 或 unintercept时，在还原我们覆盖的指令之前，需要先验证我们覆盖的指令未被再次修改，在验证过程中，由于发生 `SIGSEGV` 或 `SIGBUS` 而导致失败 |
| 29 | Verify original instruction failed | unhook 或 unintercept 时，在还原我们覆盖的指令之前，需要先验证我们覆盖的指令未被再次修改，验证失败（指令被再次修改了） |
| 30 | Verify exit instruction failed | （已废弃） |
| 31 | Verify exit instruction crashed | （已废弃） |
| 32 | Unop on an error status task | unhook 或 unintercept 时，发现对应 task 的状态是“错误”，这种情况发生在 pending task 后续被执行时发生了错误（不影响 unhook 或 unintercept 时对应资源的释放，只是表示未执行实际的 unhook 或 unintercept 操作） |
| 33 | Unop on an unfinished task | unhook 或 unintercept 时，发现对应 task 的状态是“未完成”，这种情况发生在 pending task 后续未能执行成功（不影响 unhook 或 unintercept 时对应资源的释放，只是表示未执行实际的 unhook 或 unintercept操作） |
| 34 | ELF with an unsupported architecture | hook 或 intercept 时，发现 shadowhook 不支持目标指令的指令集，导致失败（常见的情况是 shadowhook 运行在 x86 指令集上，或者是 houdini 环境中） |
| 35 | Linker with an unsupported architecture | linker 模块初始化时，发现 shadowhook 不支持 linker 目标指令的指令集，导致失败（常见的情况是 shadowhook 运行在 x86 指令集上，或者是 houdini 环境中） |
| 36 | Duplicate init fini callback | 通过 `shadowhook_register_dl_init_callback()` 或 `shadowhook_register_dl_fini_callback()` 注册回调函数时，发现重复的注册（`pre`，`post`，`data` 这 3 个参数都相同） |
| 37 | Unregister not-existed init fini callback | 通过 `shadowhook_unregister_dl_init_callback()` 或 `shadowhook_unregister_dl_fini_callback()` 反注册回调函数时，未找到对应的回调函数 |
| 38 | Register callback not supported | 通过 `shadowhook_register_dl_init_callback()` 或 `shadowhook_register_dl_fini_callback()` 注册回调函数时，发现不支持该功能（shadowhook 只在 Android 5.0 及以上操作系统版本中支持该功能） |
| 39 | Init task mod failed | task 模块初始化失败 |
| 40 | Alloc island for exit failed | 为出口跳板分配 island 失败 |
| 41 | Alloc island for enter failed | 为入口跳板分配 island 失败 |
| 42 | Alloc island for rewrite failed | 为指令重写分配 island 失败 |
| 43 | Mode conflict | hook，unhook，intercept，unintercept 时，在当前目标地址上，发生了 unique 模式与其他模式之间的冲突（比如：当前地址上已经存在了 multi 或 shared 模式的代理函数，现在想要增加 unique 模式的代理函数） |
| 44 | Duplicate multi hook | hook 时，发现在同一个目标地址上，在 multi 模式的代理函数链表中已经存在了一个相同地址的代理函数 |
| 45 | Disabled | shadowhook 已经被全局禁用，导致操作失败 |
| 100 | Load libshadowhook.so failed | 在 java 层执行 `System.loadLibrary("shadowhook");` 时，发生 JVM 异常 |
| 101 | Init exception | 在 java 层通过 `nativeInit()` JNI 调用，执行 shadowhook 的 native 层初始化时，发生 JVM 异常 |

## Java API

```Java
package com.bytedance.shadowhook;

public class ShadowHook

public static String toErrmsg(int errno)
```

- `ShadowHook.init()` 返回“非 `0`”时，表示初始化失败，此时 `ShadowHook.init()` 的返回值就是 errno。
- 通过 `toErrmsg()` 可以获取 errno 对应的 error message。

## Native API

```C
#include "shadowhook.h"

int shadowhook_get_errno(void);
const char *shadowhook_to_errmsg(int error_number);
```

- `shadowhook_init()` 返回“非0”，表示初始化失败。此时 `shadowhook_init()` 的返回值就是 errno。
- 其他 API 返回 `NULL` 或 `-1` 表示失败时，通过 `shadowhook_get_errno()` 可以获取到 errno。
- 通过 `shadowhook_to_errmsg()` 可以获取 errno 对应的 error message。


# 操作记录

> [!IMPORTANT]
> - shadowhook 会在内存中记录 hook / unhook / intercept / unintercept 的操作信息。
> - 使用者可以随时调用 API 获取这些操作记录。
> - 你可以在 app 发生崩溃时，获取这些操作记录，把它们和崩溃信息一起保存下来（或者是通过网络投递出去）。

## 操作记录格式

操作记录由 ascii 可见字符组成，每一行为一条记录，用 `\n` 换行。每行中的信息项用 `,` 分割，依次是：

| 顺序 | 名称 | 描述 | 备注 |
| :---- | :---- | :---- | :---- |
| 1 | TIMESTAMP | 时间戳 | 格式：YYYY-MM-DDThh:mm:ss.sss+hh:mm |
| 2 | CALLER\_LIB\_NAME | 调用者动态库名称 | basename |
| 3 | OP | 操作类型 | 对应了hook和intercept相关的API：<br />hook_func_addr<br />hook_sym_addr<br />hook_sym_name<br />unhook<br />intercept_instr_addr<br />intercept_func_addr<br />intercept_sym_addr<br />intercept_sym_name<br />unintercept |
| 4 | LIB_NAME | 目标函数所在动态库名称 | 操作类型 unhook 和 unintercept 时不含此项。<br />如果“操作类型”不是 hook_sym_name 或 intercept_sym_name，此项可能为 unknown。<br />只包含 basename。 |
| 5 | SYM_NAME | 目标函数名称 | 操作类型 unhook 和 unintercept 时不含此项。<br />如果“操作类型”不是 hook_sym_name 或 intercept_sym_name，此项可能为 unknown。 |
| 6 | SYM_ADDR | 目标指令或函数的地址 | 操作类型unhook和unintercept时不含此项 |
| 7 | NEW_ADDR | 代理函数或拦截器函数的地址 | 操作类型 unhook 和 unintercept 时不含此项。<br />操作类型为 hook_* 时为代理函数地址，操作类型为 intercept_* 时为拦截器函数地址。 |
| 8 | BACKUP_LEN | 目标地址被覆盖的指令长度 | 操作类型 unhook 和 unintercept 时不含此项。<br />单位：字节。 |
| 9 | ERRNO | 错误码 |  |
| 10 | STUB | hook / intercept 返回的 stub | “hook 和 unhook 之间”以及“intercept 和 unintercept 之间”可以通过这个值来配对。 |
| 11 | FLAGS | flags值 | 操作类型 unhook 和 unintercept 时不含此项。 |

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
    STUB,
    FLAGS
}
```

- `recordItems` 参数用于指定需要获取哪些操作记录项，可以传入多个 `RecordItem` 类型的参数，它们之间是 `|` 的关系；也可以不传入任何参数，以获取**所有**的操作记录项。
- `getRecords()` API 返回 `String` 类型的值，其中包含了操作记录。

## Native API

```C
#include "shadowhook.h"

// 用于指定需要获取哪些操作记录项
#define SHADOWHOOK_RECORD_ITEM_ALL             0x7FF  // 0b11111111111
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
#define SHADOWHOOK_RECORD_ITEM_FLAGS           (1 << 10)

char *shadowhook_get_records(uint32_t item_flags);
void shadowhook_dump_records(int fd, uint32_t item_flags);
```

- `item_flags` 参数用于指定需要获取哪些操作记录项，可以用 `|` 拼接上面定义的 flag；也可以指定 `SHADOWHOOK_RECORD_ITEM_ALL` 以获取**所有**的操作记录项。
- `shadowhook_get_records()` API 返回一个用 `malloc()` 分配的 buffer，其中包含了操作记录。**外部使用完后请使用 free()` 释放。**
- `shadowhook_dump_records()` API 会向 `fd` 参数所指的文件描述符写出操作记录。**这个 API 是异步信号安全的，可以在信号处理函数中调用。**


# 已知问题

## 稳定性问题

- **如果“shadowhook 的初始化时机”比“app 中 native 崩溃捕获 SDK 注册 signal handler 的时机” 更早，会导致 shadowhook 自身发生崩溃的概率上升。** 原因是在 hook 和 intercept 的执行过程中，有少量高危操作用 bytesig 做了 native 崩溃兜底（注册 `SIGSEGV` 和 `SIGBUS` 的 signal handler，崩溃时调用 `siglongjmp()`），native 崩溃捕获 SDK 也是通过注册 `SIGSEGV` 和 `SIGBUS` 的 signal handler 来收集 app 的 native 崩溃。由于后注册的 signal handler 将会先被执行，所以导致了本应该被“崩溃兜底”机制所保护的“可避免的崩溃”首先被 native 崩溃捕获 SDK 捕捉到了，认为这是真正的 app 崩溃。

- **从 shadowhook 2.0.0 版本开始，shadowhook 会使用一些 ELF 中的 useless symbol 内存空间作为 island 跳板。如果这些内存空间也被其他机制使用了，则可能会导致崩溃。** shadowhook 使用了以下的 useless symbol 内存空间（具体可参考源码 `sh_elf.c`）：

<table>
    <tr>
        <th><strong>ELF</strong></th>
        <th><strong>symbol (demangled)</strong></th>
        <th><strong>symbol</strong></th>
        <th><strong>API level</strong></th>
        <th><strong>arm64-v8</strong></th>
        <th><strong>armeabi-v7a</strong></th>
    </tr>
    <tr>
        <td rowspan="5"><code>linker/linker64</code></td>
        <td><code>__linker_init</code></td>
        <td><code>__dl___linker_init</code></td>
        <td><code>[21,36]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td rowspan="4"><code>__linker_init_post_relocation</code></td>
        <td><code>__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlockj</code></td>
        <td><code>[21,26]</code></td>
        <td></td>
        <td>✓</td>
    </tr>
    <tr>
        <td><code>__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlocky</code></td>
        <td><code>[21,26]</code></td>
        <td>✓</td>
        <td></td>
    </tr>
    <tr>
        <td><code>__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlock</code></td>
        <td><code>[27,28]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td><code>__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlockR6soinfo</code></td>
        <td><code>[29,36]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td rowspan="3"><code>libart.so</code></td>
        <td><code>art::Runtime::Start</code></td>
        <td><code>_ZN3art7Runtime5StartEv</code></td>
        <td><code>[21,36]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td rowspan="2"><code>art::Runtime::Init</code></td>
        <td><code>_ZN3art7Runtime4InitERKNSt3__16vectorINS1_4pairINS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPKvEENS7_ISC_EEEEb</code></td>
        <td><code>[21,23]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td><code>_ZN3art7Runtime4InitEONS_18RuntimeArgumentMapE</code></td>
        <td><code>[24,36]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td rowspan="5"><code>libandroid_runtime.so</code></td>
        <td rowspan="2"><code>android::AndroidRuntime::start</code></td>
        <td><code>_ZN7android14AndroidRuntime5startEPKcRKNS_6VectorINS_7String8EEE</code></td>
        <td><code>[21,22]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td><code>_ZN7android14AndroidRuntime5startEPKcRKNS_6VectorINS_7String8EEEb</code></td>
        <td><code>[23,36]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td rowspan="3"><code>android::AndroidRuntime::startVm</code></td>
        <td><code>_ZN7android14AndroidRuntime7startVmEPP7_JavaVMPP7_JNIEnv</code></td>
        <td><code>[21,22]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td><code>_ZN7android14AndroidRuntime7startVmEPP7_JavaVMPP7_JNIEnvb</code></td>
        <td><code>[23,29]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
    <tr>
        <td><code>_ZN7android14AndroidRuntime7startVmEPP7_JavaVMPP7_JNIEnvbb</code></td>
        <td><code>[30,36]</code></td>
        <td>✓</td>
        <td>✓</td>
    </tr>
</table>

- **在 32 位环境中，hook / intercept 高频调用的函数时（比如：`malloc()` / `free()` / `pthread_mutex_lock()`），有一定概率会发生崩溃（64 位 app 中不存在这个问题）。** 因为 thumb 指令一次执行 2 个字节指令，而 shadowhook 目前在执行目标地址覆盖时最少需要覆盖 4 字节指令。虽然覆盖操作本身是原子的，但由于多核 CPU 的原因，覆盖的 4 字节指令可能不能完整的被执行（CPU 可能先执行“覆盖前的 2 字节旧指令”，再执行“覆盖后的 4 字节新指令中的后 2 个字节”）。

- **在 32 位环境中，hook / intercept 某些函数或指令时，会发生必现的崩溃。** 例如下面的 32 位 libc.so 中的 `pthread_rwlock_wrlock`，如果 hook / intercept 函数的头部，此时 shadowhook 会覆盖 offset `[0008216E, 00082172)` 这 4 个字节（即 `LDR R2, [R0]` 和 `CMP R2, #3`），覆盖为一条 4 字节的相对跳转指令。但是 offset `00082194` 处的 `BEQ` 跳转指令会跳转到 offset `00082170` 处，也就是跳转到被 shadowhook 修改的 offset 范围的中间，`BEQ` 跳转之后，会执行“shadowhook 覆盖的 4 字节的跳转指令的后 2 字节”，这时程序肯定无法再正常执行了。

```Assembly
.text:0008216E ; =============== S U B R O U T I N E =======================================
.text:0008216E
.text:0008216E
.text:0008216E                 EXPORT pthread_rwlock_wrlock
.text:0008216E pthread_rwlock_wrlock                   ; CODE XREF: j_pthread_rwlock_wrlock+8↓j
.text:0008216E                                         ; DATA XREF: LOAD:00005FDC↑o ...
.text:0008216E                 LDR             R2, [R0]
.text:00082170
.text:00082170 loc_82170                               ; CODE XREF: pthread_rwlock_wrlock+26↓j
.text:00082170                 CMP             R2, #3
.text:00082172                 BHI             loc_821A4
.text:00082174                 LDAEX.W         R1, [R0]
.text:00082178                 CMP             R1, R2
.text:0008217A                 BNE             loc_8218A
.text:0008217C                 ORR.W           R2, R2, #0x80000000
.text:00082180                 STREX.W         R3, R2, [R0]
.text:00082184                 CBNZ            R3, loc_8218E
.text:00082186                 MOVS            R2, #1
.text:00082188                 B               loc_82190
.text:0008218A ; ---------------------------------------------------------------------------
.text:0008218A
.text:0008218A loc_8218A                               ; CODE XREF: pthread_rwlock_wrlock+C↑j
.text:0008218A                 CLREX.W
.text:0008218E
.text:0008218E loc_8218E                               ; CODE XREF: pthread_rwlock_wrlock+16↑j
.text:0008218E                 MOVS            R2, #0
.text:00082190
.text:00082190 loc_82190                               ; CODE XREF: pthread_rwlock_wrlock+1A↑j
.text:00082190                 CMP             R2, #0
.text:00082192                 MOV             R2, R1
.text:00082194                 BEQ             loc_82170
.text:00082196                 MRC             p15, 0, R1,c13,c0, 3
.text:0008219A                 LDR             R1, [R1,#4]
.text:0008219C                 LDR             R1, [R1,#8]
.text:0008219E                 STR             R1, [R0,#4]
.text:000821A0                 MOVS            R0, #0  ; int
.text:000821A2                 BX              LR
......
......
```

## 可用性问题

- **当一个 ELF 中已经有非常多的目标地址被 hook 或 intercept 时，继续对这个 ELF 中的目标地址执行 hook 或 intercept，可能会始终失败。这时的 errno 为 `40` 或 `41` 或 `42`。** 从 2.0.0 版本开始，shadowhook 为了优先考虑 hook 和 intercept 的稳定性，在 release 版本中，只会在目标地址处以“单条的相对地址跳转指令”的方式来实现 inlinehook，但这要求在目标地址附近（arm64 中为 `+-128MB`）分配一块额外的内存用于存放“多条的绝对地址跳转指令”，以实现二次跳转。这块内存在 shadowhook 中被称为 island。但是，可用的 island 内存空间不是无限的。例如：对于 arm64 的 ELF，在 4KB pagesize 环境中，island 内存空间平均至少可以同时容纳 256 个 hook 或 intercept；在 16KB pagesize 环境中，island 内存空间平均至少可以同时容纳 1024 个 hook 或 intercept。从 2.0.0 版本开始，shadowhook 对 libart.so, libandroid_runtime.so, linker 做了特殊处理，提高了 island 内存空间的上限。但是，一个 ELF 的 island 内存空间数量存在不确定性，在最极端的情况下，可能会遇到某个 Android 版本的某个特定机型中的某个 ELF 连一个字节的可用 island 内存空间都没有，这时，对这个 ELF 中的任何目标地址执行 hook 或 intercept 都会始终失败。所以，请不要无节制的执行太多的 hook 或 intercept，当 hook 或 intercept 的功能不再需要时，也请及时的 unhook 或 unintercept 它们。另外，也请正确处理 hook 和 intercept API 的返回值和 errno，以确保在 hook 和 intercept 失败的情况下你的程序也能继续运行。在未来版本的 shadowhook 中，我们会增加用于实时监控“可用 island 内存空间数量和分布”的 API 和工具。

- **shadowhook 不支持 x86 和 x86_64 指令集，并且，也不支持在 Houdini 环境中使用。** 在 Houdini 环境中，系统库（包括 linker，libart.so，libhoudini.so 等）都是 x86 指令的 ELF。libhoudini.so 可以理解为一个执行 arm 指令码的虚拟机。从 shadowhook 1.1.1 版本开始，shadowhook 初始化时内部会对 linker 执行 inlinehook，如果在 Houdini 环境中，由于 linker 是 x86 指令的，所以这个 inlinehook 操作会失败，导致 shadowhook 初始化失败。

## intercept 问题

- **对于指令序列中夹杂的“数据部分”，无法对它们进行拦截。** 这个应该是符合预期的。shadowhook 没有额外的逻辑去判断当前要执行 intercept 的目标地址上是“指令”还是“数据”，而是一律都认为是“指令”。请在调用 API 时不要传入“指向数据的地址”。

- **在 32 位环境中，对于“连续的两条 2 字节的 thumb 指令”或者“一条 2 字节 thumb 指令紧跟着一条 4 字节 thumb 指令”的情况，shadowhook 无法同时对它们执行intercept（只拦截其中任意的一条指令不会有问题）。** 原因是 shadowhook 处理 intercept 时会将“目标地址处的指令修改为一条 4 字节的相对跳转指令”。理论上，这种情况可以处理，但是需要增加很多特别的处理逻辑。考虑到 ROI 和目前 32 位 arm 占比已经大幅减少，目前没有支持。如果以后业务上确实有这种需求，我们再支持。

- **在 32 位环境中，对于 Thumb `IT` 指令，无法对 `THEN` 部分的指令做拦截。** 理由同上。如果以后业务上确实有这种需求，我们再支持。
