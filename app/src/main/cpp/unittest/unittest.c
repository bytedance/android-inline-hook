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

#include "unittest.h"

#include <android/api-level.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include "hookee.h"
#include "shadowhook.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"

#define LOG(fmt, ...)    __android_log_print(ANDROID_LOG_INFO, "shadowhook_tag", fmt, ##__VA_ARGS__)
#define DELIMITER        ">>>>>>>>>>>>>>>>>>> %s >>>>>>>>>>>>>>>>>>>"
#define TO_STR_HELPER(x) #x
#define TO_STR(x)        TO_STR_HELPER(x)

static bool unittest_is_hook_addr = true;

typedef int (*test_t)(int, int);

#define PROXY(inst)                                                   \
  static test_t orig_##inst = NULL;                                   \
  static void *stub_##inst = NULL;                                    \
  static int unique_proxy_##inst(int a, int b) {                      \
    LOG("proxy pre   : %-21s : %d + %d = ?", TO_STR(inst), a, b);     \
    int c = orig_##inst(a, b);                                        \
    LOG("proxy post  : %-21s : %d + %d = %d", TO_STR(inst), a, b, c); \
    return c;                                                         \
  }                                                                   \
  static int shared_proxy_##inst(int a, int b) {                      \
    LOG("proxy pre   : %-21s : %d + %d = ?", TO_STR(inst), a, b);     \
    int c = SHADOWHOOK_CALL_PREV(shared_proxy_##inst, test_t, a, b);  \
    LOG("proxy post  : %-21s : %d + %d = %d", TO_STR(inst), a, b, c); \
    SHADOWHOOK_POP_STACK();                                           \
    return c;                                                         \
  }

#define RUN(inst) LOG("--> result  : %-21s : 4 + 8 = %d", TO_STR(inst), test_##inst(4, 8))

#define RUN_WITH_DLSYM(libname, inst)                                                                \
  do {                                                                                               \
    void *handle = dlopen(TO_STR(libname), RTLD_NOW);                                                \
    if (NULL != handle) {                                                                            \
      void *func = dlsym(handle, "test_" TO_STR(inst));                                              \
      if (NULL != func) LOG("--> result  : %-21s : 4 + 8 = %d", TO_STR(inst), ((test_t)func)(4, 8)); \
      dlclose(handle);                                                                               \
      handle = NULL;                                                                                 \
    }                                                                                                \
  } while (0)

#define HOOK_WITH_TAG(inst, tag)                                                                        \
  do {                                                                                                  \
    if (NULL != stub_##inst##tag) return -1;                                                            \
    if (unittest_is_hook_addr) {                                                                        \
      if (NULL == (stub_##inst##tag = shadowhook_hook_sym_addr((void *)test_##inst,                     \
                                                               SHADOWHOOK_IS_UNIQUE_MODE                \
                                                                   ? (void *)unique_proxy_##inst##tag   \
                                                                   : (void *)shared_proxy_##inst##tag,  \
                                                               (void **)(&orig_##inst##tag)))) {        \
        LOG("unittest: hook sym addr FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                      \
      }                                                                                                 \
    } else {                                                                                            \
      if (NULL == (stub_##inst##tag = shadowhook_hook_sym_name("libhookee.so", "test_" TO_STR(inst),    \
                                                               SHADOWHOOK_IS_UNIQUE_MODE                \
                                                                   ? (void *)unique_proxy_##inst##tag   \
                                                                   : (void *)shared_proxy_##inst##tag,  \
                                                               (void **)(&orig_##inst##tag)))) {        \
        LOG("unittest: hook sym name FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                      \
      }                                                                                                 \
    }                                                                                                   \
  } while (0)
#define HOOK(inst) HOOK_WITH_TAG(inst, )
#define HOOK2(inst)                                                                                   \
  do {                                                                                                \
    if (NULL != stub_##inst) return -1;                                                               \
    if (!unittest_is_hook_addr) {                                                                     \
      if (NULL ==                                                                                     \
          (stub_##inst = shadowhook_hook_sym_name_callback(                                           \
               "libhookee2.so", "test_" TO_STR(inst),                                                 \
               SHADOWHOOK_IS_UNIQUE_MODE ? (void *)unique_proxy_##inst : (void *)shared_proxy_##inst, \
               (void **)(&orig_##inst), unittest_hooked, (void *)0x123456))) {                        \
        LOG("unittest: hook sym name FAILED: " TO_STR(inst) ". errno %d", shadowhook_get_errno());    \
        return -1;                                                                                    \
      }                                                                                               \
    }                                                                                                 \
  } while (0)

#define UNHOOK_WITH_TAG(inst, tag)                                                               \
  do {                                                                                           \
    if (NULL == stub_##inst##tag) break;                                                         \
    int r_ = shadowhook_unhook(stub_##inst##tag);                                                \
    stub_##inst##tag = NULL;                                                                     \
    if (0 != r_) {                                                                               \
      if (SHADOWHOOK_ERRNO_UNHOOK_ON_UNFINISHED != shadowhook_get_errno()) {                     \
        LOG("unittest: unhook FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                               \
      }                                                                                          \
    }                                                                                            \
  } while (0)
#define UNHOOK(inst) UNHOOK_WITH_TAG(inst, )

static void unittest_hooked(int error_number, const char *lib_name, const char *sym_name, void *sym_addr,
                            void *new_addr, void *orig_addr, void *arg) {
  LOG("unittest: hooked callback: error_number %d, lib_name %s, sym_name %s, sym_addr %p, new_addr %p, "
      "orig_addr %p, arg %p",
      error_number, lib_name, sym_name, sym_addr, new_addr, orig_addr, arg);
}

///////////////////////////////////////////////////////////////////////////
// hooking dlopen() or do_dlopen(). (only hook once)
//
// shadowhook may hook dlopen() or do_dlopen() internally.
// This unit test is to verify the scenario where they are also hooked externally.

#ifndef __LP64__
#define LINKER_BASENAME "linker"
#else
#define LINKER_BASENAME "linker64"
#endif

#define LINKER_SYM_DO_DLOPEN_L "__dl__Z9do_dlopenPKciPK17android_dlextinfo"
#define LINKER_SYM_DO_DLOPEN_N "__dl__Z9do_dlopenPKciPK17android_dlextinfoPv"
#define LINKER_SYM_DO_DLOPEN_O "__dl__Z9do_dlopenPKciPK17android_dlextinfoPKv"

typedef void *(*linker_proxy_dlopen_t)(const char *, int);
static linker_proxy_dlopen_t linker_orig_dlopen;
static void *linker_proxy_dlopen(const char *filename, int flag) {
  void *handle;
  if (SHADOWHOOK_IS_SHARED_MODE)
    handle = SHADOWHOOK_CALL_PREV(linker_proxy_dlopen, linker_proxy_dlopen_t, filename, flag);
  else
    handle = linker_orig_dlopen(filename, flag);

  if (SHADOWHOOK_IS_SHARED_MODE) SHADOWHOOK_POP_STACK();
  return handle;
}

typedef void *(*linker_proxy_do_dlopen_l_t)(const char *, int, const void *);
static linker_proxy_do_dlopen_l_t linker_orig_do_dlopen_l;
static void *linker_proxy_do_dlopen_l(const char *name, int flags, const void *extinfo) {
  void *handle;
  if (SHADOWHOOK_IS_SHARED_MODE)
    handle = SHADOWHOOK_CALL_PREV(linker_proxy_do_dlopen_l, linker_proxy_do_dlopen_l_t, name, flags, extinfo);
  else
    handle = linker_orig_do_dlopen_l(name, flags, extinfo);

  if (SHADOWHOOK_IS_SHARED_MODE) SHADOWHOOK_POP_STACK();
  return handle;
}

typedef void *(*linker_proxy_do_dlopen_n_t)(const char *, int, const void *, void *);
static linker_proxy_do_dlopen_n_t linker_orig_do_dlopen_n;
static void *linker_proxy_do_dlopen_n(const char *name, int flags, const void *extinfo, void *caller_addr) {
  void *handle;
  if (SHADOWHOOK_IS_SHARED_MODE)
    handle = SHADOWHOOK_CALL_PREV(linker_proxy_do_dlopen_n, linker_proxy_do_dlopen_n_t, name, flags, extinfo,
                                  caller_addr);
  else
    handle = linker_orig_do_dlopen_n(name, flags, extinfo, caller_addr);

  if (SHADOWHOOK_IS_SHARED_MODE) SHADOWHOOK_POP_STACK();
  return handle;
}

static int hook_dlopen(int api_level) {
  static int result = -1;
  static bool hooked = false;

  if (hooked) return result;
  hooked = true;

  void *stub;
  if (api_level < __ANDROID_API_L__) {
    stub =
        shadowhook_hook_sym_addr((void *)dlopen, (void *)linker_proxy_dlopen, (void **)&linker_orig_dlopen);
  } else {
    if (api_level >= __ANDROID_API_O__) {
      stub = shadowhook_hook_sym_name(LINKER_BASENAME, LINKER_SYM_DO_DLOPEN_O,
                                      (void *)linker_proxy_do_dlopen_n, (void **)&linker_orig_do_dlopen_n);
    } else if (api_level >= __ANDROID_API_N__) {
      stub = shadowhook_hook_sym_name(LINKER_BASENAME, LINKER_SYM_DO_DLOPEN_N,
                                      (void *)linker_proxy_do_dlopen_n, (void **)&linker_orig_do_dlopen_n);
    } else {
      stub = shadowhook_hook_sym_name(LINKER_BASENAME, LINKER_SYM_DO_DLOPEN_L,
                                      (void *)linker_proxy_do_dlopen_l, (void **)&linker_orig_do_dlopen_l);
    }
  }

  result = (NULL != stub && 0 == shadowhook_get_errno()) ? 0 : -1;
  return result;
}

// end of - hooking dlopen() or do_dlopen()
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// hooking hidden function (without symbol info in ELF)

static test_t test_hidden_func = NULL;
PROXY(hidden_func)

static void run_hidden_func(void) {
  if (NULL == test_hidden_func) test_hidden_func = (test_t)get_hidden_func_addr();
  RUN(hidden_func);
}

static int hook_hidden_func(void) {
  if (NULL == test_hidden_func) test_hidden_func = (test_t)get_hidden_func_addr();
  if (NULL != stub_hidden_func) return -1;
  stub_hidden_func = shadowhook_hook_func_addr(
      (void *)test_hidden_func,
      SHADOWHOOK_IS_UNIQUE_MODE ? (void *)unique_proxy_hidden_func : (void *)shared_proxy_hidden_func,
      (void **)(&orig_hidden_func));
  return NULL == stub_hidden_func ? -1 : 0;
}

// end of - hooking hidden function (without symbol info in ELF)
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// (1) test proxy for instructions

#if defined(__arm__)

PROXY(t16_b_t1)
PROXY(t16_b_t1_fixaddr)
PROXY(t16_b_t2)
PROXY(t16_b_t2_fixaddr)
PROXY(t16_bx_t1)
PROXY(t16_add_reg_t2)
PROXY(t16_mov_reg_t1)
PROXY(t16_adr_t1)
PROXY(t16_ldr_lit_t1)
PROXY(t16_cbz_t1)
PROXY(t16_cbz_t1_fixaddr)
PROXY(t16_cbnz_t1)
PROXY(t16_cbnz_t1_fixaddr)
PROXY(t16_it_t1_case1)
PROXY(t16_it_t1_case2)

PROXY(t32_b_t3)
PROXY(t32_b_t4)
PROXY(t32_b_t4_fixaddr)
PROXY(t32_bl_imm_t1)
PROXY(t32_blx_imm_t2)
PROXY(t32_adr_t2)
PROXY(t32_adr_t3)
PROXY(t32_ldr_lit_t2_case1)
PROXY(t32_ldr_lit_t2_case2)
PROXY(t32_pld_lit_t1)
PROXY(t32_pli_lit_t3)
PROXY(t32_tbb_t1)
PROXY(t32_tbh_t1)
PROXY(t32_vldr_lit_t1_case1)
PROXY(t32_vldr_lit_t1_case2)

PROXY(a32_b_a1)
PROXY(a32_b_a1_fixaddr)
PROXY(a32_bx_a1)
PROXY(a32_bl_imm_a1)
PROXY(a32_blx_imm_a2)
PROXY(a32_add_reg_a1_case1)
PROXY(a32_add_reg_a1_case2)
PROXY(a32_add_reg_a1_case3)
PROXY(a32_sub_reg_a1_case1)
PROXY(a32_sub_reg_a1_case2)
PROXY(a32_sub_reg_a1_case3)
PROXY(a32_adr_a1_case1)
PROXY(a32_adr_a1_case2)
PROXY(a32_adr_a2_case1)
PROXY(a32_adr_a2_case2)
PROXY(a32_mov_reg_a1_case1)
PROXY(a32_mov_reg_a1_case2)
PROXY(a32_mov_reg_a1_case3)
PROXY(a32_ldr_lit_a1_case1)
PROXY(a32_ldr_lit_a1_case2)
PROXY(a32_ldr_reg_a1_case1)
PROXY(a32_ldr_reg_a1_case2)

#elif defined(__aarch64__)

PROXY(a64_b)
PROXY(a64_b_fixaddr)
PROXY(a64_b_cond)
PROXY(a64_b_cond_fixaddr)
PROXY(a64_bl)
PROXY(a64_bl_fixaddr)
PROXY(a64_adr)
PROXY(a64_adrp)
PROXY(a64_ldr_lit_32)
PROXY(a64_ldr_lit_64)
PROXY(a64_ldrsw_lit)
PROXY(a64_prfm_lit)
PROXY(a64_ldr_simd_lit_32)
PROXY(a64_ldr_simd_lit_64)
PROXY(a64_ldr_simd_lit_128)
PROXY(a64_cbz)
PROXY(a64_cbz_fixaddr)
PROXY(a64_cbnz)
PROXY(a64_cbnz_fixaddr)
PROXY(a64_tbz)
PROXY(a64_tbz_fixaddr)
PROXY(a64_tbnz)
PROXY(a64_tbnz_fixaddr)

#endif

// end of - test proxy for instructions
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// (2) test proxy for business logic - recursion

#define PROXY_RECU(inst, inst2)                                        \
  static test_t orig_##inst = NULL;                                    \
  static void *stub_##inst = NULL;                                     \
  static int unique_proxy_##inst(int a, int b) {                       \
    LOG("recu pre    : %-21s : %d + %d = ?", TO_STR(inst2), a, b);     \
    int c = test_##inst2(a, b);                                        \
    LOG("recu post   : %-21s : %d + %d = %d", TO_STR(inst2), a, b, c); \
    LOG("proxy pre   : %-21s : %d + %d = ?", TO_STR(inst), a, b);      \
    c = orig_##inst(a, b);                                             \
    LOG("proxy post  : %-21s : %d + %d = %d", TO_STR(inst), a, b, c);  \
    return c;                                                          \
  }                                                                    \
  static int shared_proxy_##inst(int a, int b) {                       \
    LOG("recu pre    : %-21s : %d + %d = ?", TO_STR(inst2), a, b);     \
    int c = test_##inst2(a, b);                                        \
    LOG("recu post   : %-21s : %d + %d = %d", TO_STR(inst2), a, b, c); \
    LOG("proxy pre   : %-21s : %d + %d = ?", TO_STR(inst), a, b);      \
    c = SHADOWHOOK_CALL_PREV(shared_proxy_##inst, test_t, a, b);       \
    LOG("proxy post  : %-21s : %d + %d = %d", TO_STR(inst), a, b, c);  \
    SHADOWHOOK_POP_STACK();                                            \
    return c;                                                          \
  }

PROXY_RECU(recursion_1, recursion_2)
PROXY_RECU(recursion_2, recursion_1)

// end of - test proxy for business logic - recursion
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// (3) test proxy for business logic - hook multiple times

PROXY(hook_multi_times_1)
PROXY(hook_multi_times_2)
PROXY(hook_multi_times_3)

// end of - test proxy for business logic - hook multiple times
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// (4) test proxy for business logic - hook multiple times

PROXY(hook_before_dlopen_1)
PROXY(hook_before_dlopen_2)

// end of - test proxy for business logic - hook multiple times
///////////////////////////////////////////////////////////////////////////

static int unittest_hook(int api_level) {
  if (0 != hook_dlopen(api_level)) {
    LOG("hook dlopen() / dl_dlopen() FAILED");
    return -1;
  }

#if defined(__arm__)

  HOOK(t16_b_t1);
  HOOK(t16_b_t1_fixaddr);
  HOOK(t16_b_t2);
  HOOK(t16_b_t2_fixaddr);
  HOOK(t16_bx_t1);
  HOOK(t16_add_reg_t2);
  HOOK(t16_mov_reg_t1);
  HOOK(t16_adr_t1);
  HOOK(t16_ldr_lit_t1);
  HOOK(t16_cbz_t1);
  HOOK(t16_cbz_t1_fixaddr);
  HOOK(t16_cbnz_t1);
  HOOK(t16_cbnz_t1_fixaddr);
  HOOK(t16_it_t1_case1);
  HOOK(t16_it_t1_case2);

  HOOK(t32_b_t3);
  HOOK(t32_b_t4);
  HOOK(t32_b_t4_fixaddr);
  HOOK(t32_bl_imm_t1);
  HOOK(t32_blx_imm_t2);
  HOOK(t32_adr_t2);
  HOOK(t32_adr_t3);
  HOOK(t32_ldr_lit_t2_case1);
  HOOK(t32_ldr_lit_t2_case2);
  HOOK(t32_pld_lit_t1);
  HOOK(t32_pli_lit_t3);
  HOOK(t32_tbb_t1);
  HOOK(t32_tbh_t1);
  HOOK(t32_vldr_lit_t1_case1);
  HOOK(t32_vldr_lit_t1_case2);

  HOOK(a32_b_a1);
  HOOK(a32_b_a1_fixaddr);
  HOOK(a32_bx_a1);
  HOOK(a32_bl_imm_a1);
  HOOK(a32_blx_imm_a2);
  HOOK(a32_add_reg_a1_case1);
  HOOK(a32_add_reg_a1_case2);
  HOOK(a32_add_reg_a1_case3);
  HOOK(a32_sub_reg_a1_case1);
  HOOK(a32_sub_reg_a1_case2);
  HOOK(a32_sub_reg_a1_case3);
  HOOK(a32_adr_a1_case1);
  HOOK(a32_adr_a1_case2);
  HOOK(a32_adr_a2_case1);
  HOOK(a32_adr_a2_case2);
  HOOK(a32_mov_reg_a1_case1);
  HOOK(a32_mov_reg_a1_case2);
  HOOK(a32_mov_reg_a1_case3);
  HOOK(a32_ldr_lit_a1_case1);
  HOOK(a32_ldr_lit_a1_case2);
  HOOK(a32_ldr_reg_a1_case1);
  HOOK(a32_ldr_reg_a1_case2);

#elif defined(__aarch64__)

  HOOK(a64_b);
  HOOK(a64_b_fixaddr);
  HOOK(a64_b_cond);
  HOOK(a64_b_cond_fixaddr);
  HOOK(a64_bl);
  HOOK(a64_bl_fixaddr);
  HOOK(a64_adr);
  HOOK(a64_adrp);
  HOOK(a64_ldr_lit_32);
  HOOK(a64_ldr_lit_64);
  HOOK(a64_ldrsw_lit);
  HOOK(a64_prfm_lit);
  HOOK(a64_ldr_simd_lit_32);
  HOOK(a64_ldr_simd_lit_64);
  HOOK(a64_ldr_simd_lit_128);
  HOOK(a64_cbz);
  HOOK(a64_cbz_fixaddr);
  HOOK(a64_cbnz);
  HOOK(a64_cbnz_fixaddr);
  HOOK(a64_tbz);
  HOOK(a64_tbz_fixaddr);
  HOOK(a64_tbnz);
  HOOK(a64_tbnz_fixaddr);

#endif

  if (unittest_is_hook_addr) {
    if (0 != hook_hidden_func()) {
      LOG("hook hidden function FAILED");
      return -1;
    }
  }

  if (SHADOWHOOK_IS_SHARED_MODE) {
    HOOK(recursion_1);
    HOOK(recursion_2);

    HOOK_WITH_TAG(hook_multi_times, _1);
    HOOK_WITH_TAG(hook_multi_times, _2);
    HOOK_WITH_TAG(hook_multi_times, _3);
  }

  HOOK2(hook_before_dlopen_1);
  HOOK2(hook_before_dlopen_2);

  return 0;
}

int unittest_hook_sym_addr(int api_level) {
  LOG("*** UNIT TEST: hook symbol address (%s mode) ***", SHADOWHOOK_IS_SHARED_MODE ? "SHARED" : "UNIQUE");
  unittest_is_hook_addr = true;
  return unittest_hook(api_level);
}

int unittest_hook_sym_name(int api_level) {
  LOG("*** UNIT TEST: hook symbol name (%s mode) ***", SHADOWHOOK_IS_SHARED_MODE ? "SHARED" : "UNIQUE");
  unittest_is_hook_addr = false;
  return unittest_hook(api_level);
}

int unittest_unhook(void) {
  LOG("*** UNIT TEST: unhook symbol %s (%s mode) ***", unittest_is_hook_addr ? "address" : "name",
      SHADOWHOOK_IS_SHARED_MODE ? "SHARED" : "UNIQUE");

#if defined(__arm__)

  UNHOOK(t16_b_t1);
  UNHOOK(t16_b_t1_fixaddr);
  UNHOOK(t16_b_t2);
  UNHOOK(t16_b_t2_fixaddr);
  UNHOOK(t16_bx_t1);
  UNHOOK(t16_add_reg_t2);
  UNHOOK(t16_mov_reg_t1);
  UNHOOK(t16_adr_t1);
  UNHOOK(t16_ldr_lit_t1);
  UNHOOK(t16_cbz_t1);
  UNHOOK(t16_cbz_t1_fixaddr);
  UNHOOK(t16_cbnz_t1);
  UNHOOK(t16_cbnz_t1_fixaddr);
  UNHOOK(t16_it_t1_case1);
  UNHOOK(t16_it_t1_case2);

  UNHOOK(t32_b_t3);
  UNHOOK(t32_b_t4);
  UNHOOK(t32_b_t4_fixaddr);
  UNHOOK(t32_bl_imm_t1);
  UNHOOK(t32_blx_imm_t2);
  UNHOOK(t32_adr_t2);
  UNHOOK(t32_adr_t3);
  UNHOOK(t32_ldr_lit_t2_case1);
  UNHOOK(t32_ldr_lit_t2_case2);
  UNHOOK(t32_pld_lit_t1);
  UNHOOK(t32_pli_lit_t3);
  UNHOOK(t32_tbb_t1);
  UNHOOK(t32_tbh_t1);
  UNHOOK(t32_vldr_lit_t1_case1);
  UNHOOK(t32_vldr_lit_t1_case2);

  UNHOOK(a32_b_a1);
  UNHOOK(a32_b_a1_fixaddr);
  UNHOOK(a32_bx_a1);
  UNHOOK(a32_bl_imm_a1);
  UNHOOK(a32_blx_imm_a2);
  UNHOOK(a32_add_reg_a1_case1);
  UNHOOK(a32_add_reg_a1_case2);
  UNHOOK(a32_add_reg_a1_case3);
  UNHOOK(a32_sub_reg_a1_case1);
  UNHOOK(a32_sub_reg_a1_case2);
  UNHOOK(a32_sub_reg_a1_case3);
  UNHOOK(a32_adr_a1_case1);
  UNHOOK(a32_adr_a1_case2);
  UNHOOK(a32_adr_a2_case1);
  UNHOOK(a32_adr_a2_case2);
  UNHOOK(a32_mov_reg_a1_case1);
  UNHOOK(a32_mov_reg_a1_case2);
  UNHOOK(a32_mov_reg_a1_case3);
  UNHOOK(a32_ldr_lit_a1_case1);
  UNHOOK(a32_ldr_lit_a1_case2);
  UNHOOK(a32_ldr_reg_a1_case1);
  UNHOOK(a32_ldr_reg_a1_case2);

#elif defined(__aarch64__)

  UNHOOK(a64_b);
  UNHOOK(a64_b_fixaddr);
  UNHOOK(a64_b_cond);
  UNHOOK(a64_b_cond_fixaddr);
  UNHOOK(a64_bl);
  UNHOOK(a64_bl_fixaddr);
  UNHOOK(a64_adr);
  UNHOOK(a64_adrp);
  UNHOOK(a64_ldr_lit_32);
  UNHOOK(a64_ldr_lit_64);
  UNHOOK(a64_ldrsw_lit);
  UNHOOK(a64_prfm_lit);
  UNHOOK(a64_ldr_simd_lit_32);
  UNHOOK(a64_ldr_simd_lit_64);
  UNHOOK(a64_ldr_simd_lit_128);
  UNHOOK(a64_cbz);
  UNHOOK(a64_cbz_fixaddr);
  UNHOOK(a64_cbnz);
  UNHOOK(a64_cbnz_fixaddr);
  UNHOOK(a64_tbz);
  UNHOOK(a64_tbz_fixaddr);
  UNHOOK(a64_tbnz);
  UNHOOK(a64_tbnz_fixaddr);

#endif

  UNHOOK(hidden_func);

  if (SHADOWHOOK_IS_SHARED_MODE) {
    UNHOOK(recursion_1);
    UNHOOK(recursion_2);

    UNHOOK_WITH_TAG(hook_multi_times, _2);
    UNHOOK_WITH_TAG(hook_multi_times, _3);
    UNHOOK_WITH_TAG(hook_multi_times, _1);
  }

  UNHOOK(hook_before_dlopen_1);
  UNHOOK(hook_before_dlopen_2);

  return 0;
}

int unittest_run(bool hookee2_loaded) {
  LOG("*** UNIT TEST: run (%s mode) ***", SHADOWHOOK_IS_SHARED_MODE ? "SHARED" : "UNIQUE");

#if defined(__arm__)

  LOG(DELIMITER, "TEST INST T16");
  RUN(t16_b_t1);
  RUN(t16_b_t1_fixaddr);
  RUN(t16_b_t2);
  RUN(t16_b_t2_fixaddr);
  RUN(t16_bx_t1);
  RUN(t16_add_reg_t2);
  RUN(t16_mov_reg_t1);
  RUN(t16_adr_t1);
  RUN(t16_ldr_lit_t1);
  RUN(t16_cbz_t1);
  RUN(t16_cbz_t1_fixaddr);
  RUN(t16_cbnz_t1);
  RUN(t16_cbnz_t1_fixaddr);
  RUN(t16_it_t1_case1);
  RUN(t16_it_t1_case2);

  LOG(DELIMITER, "TEST INST T32");
  RUN(t32_b_t3);
  RUN(t32_b_t4);
  RUN(t32_b_t4_fixaddr);
  RUN(t32_bl_imm_t1);
  RUN(t32_blx_imm_t2);
  RUN(t32_adr_t2);
  RUN(t32_adr_t3);
  RUN(t32_ldr_lit_t2_case1);
  RUN(t32_ldr_lit_t2_case2);
  RUN(t32_pld_lit_t1);
  RUN(t32_pli_lit_t3);
  RUN(t32_tbb_t1);
  RUN(t32_tbh_t1);
  RUN(t32_vldr_lit_t1_case1);
  RUN(t32_vldr_lit_t1_case2);

  LOG(DELIMITER, "TEST INST A32");
  RUN(a32_b_a1);
  RUN(a32_b_a1_fixaddr);
  RUN(a32_bx_a1);
  RUN(a32_bl_imm_a1);
  RUN(a32_blx_imm_a2);
  RUN(a32_add_reg_a1_case1);
  RUN(a32_add_reg_a1_case2);
  RUN(a32_add_reg_a1_case3);
  RUN(a32_sub_reg_a1_case1);
  RUN(a32_sub_reg_a1_case2);
  RUN(a32_sub_reg_a1_case3);
  RUN(a32_adr_a1_case1);
  RUN(a32_adr_a1_case2);
  RUN(a32_adr_a2_case1);
  RUN(a32_adr_a2_case2);
  RUN(a32_mov_reg_a1_case1);
  RUN(a32_mov_reg_a1_case2);
  RUN(a32_mov_reg_a1_case3);
  RUN(a32_ldr_lit_a1_case1);
  RUN(a32_ldr_lit_a1_case2);
  RUN(a32_ldr_reg_a1_case1);
  RUN(a32_ldr_reg_a1_case2);

#elif defined(__aarch64__)

  LOG(DELIMITER, "TEST INST A64");
  RUN(a64_b);
  RUN(a64_b_fixaddr);
  RUN(a64_b_cond);
  RUN(a64_b_cond_fixaddr);
  RUN(a64_bl);
  RUN(a64_bl_fixaddr);
  RUN(a64_adr);
  RUN(a64_adrp);
  RUN(a64_ldr_lit_32);
  RUN(a64_ldr_lit_64);
  RUN(a64_ldrsw_lit);
  RUN(a64_prfm_lit);
  RUN(a64_ldr_simd_lit_32);
  RUN(a64_ldr_simd_lit_64);
  RUN(a64_ldr_simd_lit_128);
  RUN(a64_cbz);
  RUN(a64_cbz_fixaddr);
  RUN(a64_cbnz);
  RUN(a64_cbnz_fixaddr);
  RUN(a64_tbz);
  RUN(a64_tbz_fixaddr);
  RUN(a64_tbnz);
  RUN(a64_tbnz_fixaddr);

#endif

  LOG(DELIMITER, "TEST - hidden function");
  run_hidden_func();

  if (SHADOWHOOK_IS_SHARED_MODE) {
    LOG(DELIMITER, "TEST - recursion");
    RUN(recursion_1);
    LOG(DELIMITER, "TEST - hook multi times");
    RUN(hook_multi_times);
  }

  if (hookee2_loaded) {
    LOG(DELIMITER, "TEST - hook before dlopen");
    RUN_WITH_DLSYM(libhookee2.so, hook_before_dlopen_1);
    RUN_WITH_DLSYM(libhookee2.so, hook_before_dlopen_2);
  }

  return 0;
}

#pragma clang diagnostic pop
