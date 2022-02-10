// Copyright (c) 2021-2022 ByteDance Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

// Created by Kelun Cai (caikelun@bytedance.com) on 2021-04-11.

#include <stddef.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pthread.h>
#include "shadowhook.h"
#include "sh_linker.h"
#include "sh_switch.h"
#include "sh_recorder.h"
#include "sh_util.h"
#include "sh_log.h"
#include "xdl.h"

#ifndef __LP64__
#define SH_LINKER_BASENAME "linker"
#else
#define SH_LINKER_BASENAME "linker64"
#endif

#define SH_LINKER_SYM_G_DL_MUTEX  "__dl__ZL10g_dl_mutex"
#define SH_LINKER_SYM_DO_DLOPEN_L "__dl__Z9do_dlopenPKciPK17android_dlextinfo"
#define SH_LINKER_SYM_DO_DLOPEN_N "__dl__Z9do_dlopenPKciPK17android_dlextinfoPv"
#define SH_LINKER_SYM_DO_DLOPEN_O "__dl__Z9do_dlopenPKciPK17android_dlextinfoPKv"

static bool sh_linker_hooked = false;

static sh_linker_post_dlopen_t sh_linker_post_dlopen;
static void *sh_linker_post_dlopen_arg;

static pthread_mutex_t *sh_linker_g_dl_mutex;
static uintptr_t sh_linker_monitor_addr; // save address of dlopen(==4.x) or do_dlopen(>=5.0)

#if defined(__arm__) && __ANDROID_API__ < __ANDROID_API_L__
static uintptr_t sh_linker_dlfcn[6];
static const char *sh_linker_dlfcn_name[6] = {
    "dlopen",
    "dlerror",
    "dlsym",
    "dladdr",
    "dlclose",
    "dl_unwind_find_exidx"
};
#endif

__attribute__((constructor)) static void sh_linker_ctor(void)
{
    sh_linker_monitor_addr = (uintptr_t)dlopen;
#if defined(__arm__) && __ANDROID_API__ < __ANDROID_API_L__
    sh_linker_dlfcn[0] = (uintptr_t)dlopen;
    sh_linker_dlfcn[1] = (uintptr_t)dlerror;
    sh_linker_dlfcn[2] = (uintptr_t)dlsym;
    sh_linker_dlfcn[3] = (uintptr_t)dladdr;
    sh_linker_dlfcn[4] = (uintptr_t)dlclose;
    sh_linker_dlfcn[5] = (uintptr_t)dl_unwind_find_exidx;
#endif
}

int sh_linker_init(void)
{
    int api_level = sh_util_get_api_level();
    if(api_level >= __ANDROID_API_L__)
    {
        sh_linker_monitor_addr = 0;

        void *handle = xdl_open(SH_LINKER_BASENAME, XDL_DEFAULT);
        if(NULL == handle) return -1;

        sh_linker_g_dl_mutex = (pthread_mutex_t *)(xdl_dsym(handle, SH_LINKER_SYM_G_DL_MUTEX, NULL));

        if(api_level >= __ANDROID_API_O__)
            sh_linker_monitor_addr = (uintptr_t)(xdl_dsym(handle, SH_LINKER_SYM_DO_DLOPEN_O, NULL));
        else if(api_level >= __ANDROID_API_N__)
            sh_linker_monitor_addr = (uintptr_t)(xdl_dsym(handle, SH_LINKER_SYM_DO_DLOPEN_N, NULL));
        else
            sh_linker_monitor_addr = (uintptr_t)(xdl_dsym(handle, SH_LINKER_SYM_DO_DLOPEN_L, NULL));

        xdl_close(handle);
    }

    return (0 != sh_linker_monitor_addr && (0 != sh_linker_g_dl_mutex || api_level < __ANDROID_API_L__)) ? 0 : -1;
}

const char *sh_linker_match_dlfcn(uintptr_t target_addr)
{
#if defined(__arm__) && __ANDROID_API__ < __ANDROID_API_L__
    if(sh_util_get_api_level() < __ANDROID_API_L__)
        for(size_t i = 0; i < sizeof(sh_linker_dlfcn) / sizeof(sh_linker_dlfcn[0]); i++)
            if(sh_linker_dlfcn[i] == target_addr) return sh_linker_dlfcn_name[i];
#else
    (void)target_addr;
#endif

    return NULL;
}

bool sh_linker_need_to_hook_dlopen(uintptr_t target_addr)
{
    return SHADOWHOOK_IS_UNIQUE_MODE && !sh_linker_hooked && target_addr == sh_linker_monitor_addr;
}

typedef void *(*sh_linker_proxy_dlopen_t)(const char*, int);
static sh_linker_proxy_dlopen_t sh_linker_orig_dlopen;
static void *sh_linker_proxy_dlopen(const char* filename, int flag)
{
    void *handle;
    if(SHADOWHOOK_IS_SHARED_MODE)
        handle = SHADOWHOOK_CALL_PREV(sh_linker_proxy_dlopen, sh_linker_proxy_dlopen_t, filename, flag);
    else
        handle = sh_linker_orig_dlopen(filename, flag);

    if(NULL != handle) sh_linker_post_dlopen(sh_linker_post_dlopen_arg);

    if(SHADOWHOOK_IS_SHARED_MODE) SHADOWHOOK_POP_STACK();
    return handle;
}

typedef void *(*sh_linker_proxy_do_dlopen_l_t)(const char*, int, const void*);
static sh_linker_proxy_do_dlopen_l_t sh_linker_orig_do_dlopen_l;
static void *sh_linker_proxy_do_dlopen_l(const char* name, int flags, const void* extinfo)
{
    void *handle;
    if(SHADOWHOOK_IS_SHARED_MODE)
        handle = SHADOWHOOK_CALL_PREV(sh_linker_proxy_do_dlopen_l, sh_linker_proxy_do_dlopen_l_t, name, flags, extinfo);
    else
        handle = sh_linker_orig_do_dlopen_l(name, flags, extinfo);

    if(NULL != handle) sh_linker_post_dlopen(sh_linker_post_dlopen_arg);

    if(SHADOWHOOK_IS_SHARED_MODE) SHADOWHOOK_POP_STACK();
    return handle;
}

typedef void *(*sh_linker_proxy_do_dlopen_n_t)(const char*, int, const void*, void*);
static sh_linker_proxy_do_dlopen_n_t sh_linker_orig_do_dlopen_n;
static void *sh_linker_proxy_do_dlopen_n(const char* name, int flags, const void* extinfo, void* caller_addr)
{
    void *handle;
    if(SHADOWHOOK_IS_SHARED_MODE)
        handle = SHADOWHOOK_CALL_PREV(sh_linker_proxy_do_dlopen_n, sh_linker_proxy_do_dlopen_n_t, name, flags, extinfo, caller_addr);
    else
        handle = sh_linker_orig_do_dlopen_n(name, flags, extinfo, caller_addr);

    if(NULL != handle) sh_linker_post_dlopen(sh_linker_post_dlopen_arg);

    if(SHADOWHOOK_IS_SHARED_MODE) SHADOWHOOK_POP_STACK();
    return handle;
}

int sh_linker_hook_dlopen(sh_linker_post_dlopen_t post_dlopen, void *post_dlopen_arg)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static int hook_result = SHADOWHOOK_ERRNO_MONITOR_DLOPEN;

    if(sh_linker_hooked) return hook_result;
    pthread_mutex_lock(&lock);
    if(sh_linker_hooked) goto end;

    // do init for SHARED mode
    if(SHADOWHOOK_IS_SHARED_MODE)
        if(0 != sh_linker_init()) goto end;

    // save post callback ptr before hooking
    sh_linker_post_dlopen = post_dlopen;
    sh_linker_post_dlopen_arg = post_dlopen_arg;

    // hook for dlopen() or do_dlopen()
    int (*hook)(uintptr_t, uintptr_t, uintptr_t *, char *, size_t, char *, size_t, size_t *) = SHADOWHOOK_IS_SHARED_MODE ? sh_switch_hook : sh_switch_hook_invisible;
    int api_level = sh_util_get_api_level();
    size_t backup_len = 0;
    int hook_r;
    if(api_level < __ANDROID_API_L__)
    {
        hook_r = hook(sh_linker_monitor_addr, (uintptr_t)sh_linker_proxy_dlopen, (uintptr_t *)&sh_linker_orig_dlopen, NULL, 0, NULL, 0, &backup_len);
        sh_recorder_add_hook(hook_r, true, sh_linker_monitor_addr, SH_LINKER_BASENAME, "dlopen", (uintptr_t)sh_linker_proxy_dlopen, backup_len, UINTPTR_MAX, (uintptr_t)(__builtin_return_address(0)));
        if(0 != hook_r) goto end;
    }
    else
    {
        uintptr_t proxy;
        uintptr_t *orig;
        if(api_level >= __ANDROID_API_N__)
        {
            proxy = (uintptr_t)sh_linker_proxy_do_dlopen_n;
            orig = (uintptr_t *)&sh_linker_orig_do_dlopen_n;
        }
        else
        {
            proxy = (uintptr_t)sh_linker_proxy_do_dlopen_l;
            orig = (uintptr_t *)&sh_linker_orig_do_dlopen_l;
        }

        // hook
        pthread_mutex_lock(sh_linker_g_dl_mutex);
        hook_r = hook(sh_linker_monitor_addr, proxy, orig, NULL, 0, NULL, 0, &backup_len);
        pthread_mutex_unlock(sh_linker_g_dl_mutex);

        // record
        char *sym_name;
        if(api_level >= __ANDROID_API_O__)
            sym_name = SH_LINKER_SYM_DO_DLOPEN_O;
        else if(api_level >= __ANDROID_API_N__)
            sym_name = SH_LINKER_SYM_DO_DLOPEN_N;
        else
            sym_name = SH_LINKER_SYM_DO_DLOPEN_L;
        sh_recorder_add_hook(hook_r, true, sh_linker_monitor_addr, SH_LINKER_BASENAME, sym_name, proxy, backup_len, UINTPTR_MAX, (uintptr_t)(__builtin_return_address(0)));

        if(0 != hook_r) goto end;
    }

    // OK
    hook_result = 0;
    sh_linker_hooked = true;

end:
    pthread_mutex_unlock(&lock);
    SH_LOG_INFO("linker: hook dlopen %s, return: %d", 0 == hook_result ? "OK" : "FAILED", hook_result);
    return hook_result;
}
