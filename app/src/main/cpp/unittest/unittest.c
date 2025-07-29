// Copyright (c) 2021-2025 ByteDance Inc.
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#pragma clang diagnostic pop

#include "unittest.h"

#include <android/api-level.h>
#include <android/log.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
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
static bool unittest_is_intercept_addr = true;
static bool unittest_is_benchmark = false;
static uint32_t unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_DEFAULT;

typedef int (*test_t)(int, int);

// global definition
#define GLOBL_DEF(inst)                                                                        \
  static void *stub_hook_##inst = NULL;                                                        \
  static test_t orig_##inst = NULL;                                                            \
  static int non_shared_proxy_##inst(int a, int b) {                                           \
    if (__predict_false(!unittest_is_benchmark))                                               \
      LOG("proxy pre   : %-21s : %d + %d = ?", TO_STR(inst), a, b);                            \
    int c = orig_##inst(a, b);                                                                 \
    if (__predict_false(!unittest_is_benchmark))                                               \
      LOG("proxy post  : %-21s : %d + %d = %d", TO_STR(inst), a, b, c);                        \
    return c;                                                                                  \
  }                                                                                            \
  static int shared_proxy_##inst(int a, int b) {                                               \
    if (__predict_false(!unittest_is_benchmark))                                               \
      LOG("proxy pre   : %-21s : %d + %d = ?", TO_STR(inst), a, b);                            \
    int c = SHADOWHOOK_CALL_PREV(shared_proxy_##inst, test_t, a, b);                           \
    if (__predict_false(!unittest_is_benchmark))                                               \
      LOG("proxy post  : %-21s : %d + %d = %d", TO_STR(inst), a, b, c);                        \
    SHADOWHOOK_POP_STACK();                                                                    \
    return c;                                                                                  \
  }                                                                                            \
  static void *stub_intercept_##inst = NULL;                                                   \
  static void interceptor_##inst(shadowhook_cpu_context_t *ctx, void *data) {                  \
    (void)data;                                                                                \
    /*ctx->regs[0] = 20;*/                                                                     \
    /*ctx->regs[1] = 30;*/                                                                     \
    if (__predict_false(!unittest_is_benchmark))                                               \
      LOG("interceptor : %-21s : %" PRIxPTR " + %" PRIxPTR " = ?", TO_STR(inst), ctx->regs[0], \
          ctx->regs[1]);                                                                       \
  }

// run
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

// hook
#define HOOK_WITH_TAG(inst, tag)                                                                        \
  do {                                                                                                  \
    if (NULL != stub_hook_##inst##tag) return -1;                                                       \
    if (unittest_is_hook_addr) {                                                                        \
      if (NULL == (stub_hook_##inst##tag = shadowhook_hook_sym_addr_2(                                  \
                       (void *)test_##inst,                                                             \
                       SHADOWHOOK_IS_SHARED_MODE ? (void *)shared_proxy_##inst##tag                     \
                                                 : (void *)non_shared_proxy_##inst##tag,                \
                       (void **)(&orig_##inst##tag), SHADOWHOOK_HOOK_RECORD, "libhookee.so",            \
                       "test_" TO_STR(inst)))) {                                                        \
        LOG("unittest: hook sym addr FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                      \
      }                                                                                                 \
    } else {                                                                                            \
      if (NULL == (stub_hook_##inst##tag = shadowhook_hook_sym_name(                                    \
                       "libhookee.so", "test_" TO_STR(inst),                                            \
                       SHADOWHOOK_IS_SHARED_MODE ? (void *)shared_proxy_##inst##tag                     \
                                                 : (void *)non_shared_proxy_##inst##tag,                \
                       (void **)(&orig_##inst##tag)))) {                                                \
        LOG("unittest: hook sym name FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                      \
      }                                                                                                 \
    }                                                                                                   \
  } while (0)
#define HOOK(inst) HOOK_WITH_TAG(inst, )
#define HOOK_PENDING(inst)                                                                                \
  do {                                                                                                    \
    if (NULL != stub_hook_##inst) return -1;                                                              \
    if (!unittest_is_hook_addr) {                                                                         \
      if (NULL ==                                                                                         \
          (stub_hook_##inst = shadowhook_hook_sym_name_callback(                                          \
               "libhookee2.so", "test_" TO_STR(inst),                                                     \
               SHADOWHOOK_IS_SHARED_MODE ? (void *)shared_proxy_##inst : (void *)non_shared_proxy_##inst, \
               (void **)(&orig_##inst), unittest_hooked, (void *)0x123456))) {                            \
        LOG("unittest: hook sym name FAILED: " TO_STR(inst) ". errno %d", shadowhook_get_errno());        \
        return -1;                                                                                        \
      }                                                                                                   \
    }                                                                                                     \
  } while (0)

// hook_2
#define HOOK2_WITH_TAG(inst, flags, tag)                                                                 \
  do {                                                                                                   \
    if (NULL != stub_hook_##inst##tag) return -1;                                                        \
    if (unittest_is_hook_addr) {                                                                         \
      if (NULL == (stub_hook_##inst##tag = shadowhook_hook_sym_addr_2(                                   \
                       (void *)test_##inst,                                                              \
                       SHADOWHOOK_HOOK_WITH_SHARED_MODE == flags ? (void *)shared_proxy_##inst##tag      \
                                                                 : (void *)non_shared_proxy_##inst##tag, \
                       (void **)(&orig_##inst##tag), flags | SHADOWHOOK_HOOK_RECORD, "libhookee.so",     \
                       "test_" TO_STR(inst)))) {                                                         \
        LOG("unittest: hook sym addr FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno());  \
        return -1;                                                                                       \
      }                                                                                                  \
    } else {                                                                                             \
      if (NULL == (stub_hook_##inst##tag = shadowhook_hook_sym_name_2(                                   \
                       "libhookee.so", "test_" TO_STR(inst),                                             \
                       SHADOWHOOK_HOOK_WITH_SHARED_MODE == flags ? (void *)shared_proxy_##inst##tag      \
                                                                 : (void *)non_shared_proxy_##inst##tag, \
                       (void **)(&orig_##inst##tag), flags))) {                                          \
        LOG("unittest: hook sym name FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno());  \
        return -1;                                                                                       \
      }                                                                                                  \
    }                                                                                                    \
  } while (0)
#define HOOK_SHARED(inst)               HOOK2_WITH_TAG(inst, SHADOWHOOK_HOOK_WITH_SHARED_MODE, )
#define HOOK_UNIQUE(inst)               HOOK2_WITH_TAG(inst, SHADOWHOOK_HOOK_WITH_UNIQUE_MODE, )
#define HOOK_MULTI(inst)                HOOK2_WITH_TAG(inst, SHADOWHOOK_HOOK_WITH_MULTI_MODE, )
#define HOOK_SHARED_WITH_TAG(inst, tag) HOOK2_WITH_TAG(inst, SHADOWHOOK_HOOK_WITH_SHARED_MODE, tag)
#define HOOK_UNIQUE_WITH_TAG(inst, tag) HOOK2_WITH_TAG(inst, SHADOWHOOK_HOOK_WITH_UNIQUE_MODE, tag)
#define HOOK_MULTI_WITH_TAG(inst, tag)  HOOK2_WITH_TAG(inst, SHADOWHOOK_HOOK_WITH_MULTI_MODE, tag)

static void unittest_hooked(int error_number, const char *lib_name, const char *sym_name, void *sym_addr,
                            void *new_addr, void *orig_addr, void *arg) {
  LOG("unittest: hooked callback: error_number %d, lib_name %s, sym_name %s, sym_addr %p, new_addr %p, "
      "orig_addr %p, arg %p",
      error_number, lib_name, sym_name, sym_addr, new_addr, orig_addr, arg);
}

// unhook
#define UNHOOK_WITH_TAG(inst, tag)                                                               \
  do {                                                                                           \
    if (NULL == stub_hook_##inst##tag) break;                                                    \
    int r_ = shadowhook_unhook(stub_hook_##inst##tag);                                           \
    stub_hook_##inst##tag = NULL;                                                                \
    if (0 != r_) {                                                                               \
      if (SHADOWHOOK_ERRNO_UNHOOK_ON_UNFINISHED != shadowhook_get_errno()) {                     \
        LOG("unittest: unhook FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                               \
      }                                                                                          \
    }                                                                                            \
  } while (0)
#define UNHOOK(inst) UNHOOK_WITH_TAG(inst, )

// intercept
#define INTERCEPT_WITH_TAG(inst, tag)                                                                        \
  do {                                                                                                       \
    if (NULL != stub_intercept_##inst##tag) return -1;                                                       \
    if (unittest_is_intercept_addr) {                                                                        \
      if (NULL == (stub_intercept_##inst##tag = shadowhook_intercept_sym_addr(                               \
                       (void *)test_##inst, interceptor_##inst##tag, NULL,                                   \
                       unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libhookee.so",             \
                       "test_" TO_STR(inst)))) {                                                             \
        LOG("unittest: intercept sym addr FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                           \
      }                                                                                                      \
    } else {                                                                                                 \
      if (NULL == (stub_intercept_##inst##tag = shadowhook_intercept_sym_name(                               \
                       "libhookee.so", "test_" TO_STR(inst), interceptor_##inst##tag, NULL,                  \
                       unittest_interceptor_flags))) {                                                       \
        LOG("unittest: intercept sym name FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                           \
      }                                                                                                      \
    }                                                                                                        \
  } while (0)
#define INTERCEPT(inst) INTERCEPT_WITH_TAG(inst, )
#define INTERCEPT2(inst)                                                                           \
  do {                                                                                             \
    if (NULL != stub_intercept_##inst) return -1;                                                  \
    if (!unittest_is_intercept_addr) {                                                             \
      if (NULL == (stub_intercept_##inst = shadowhook_intercept_sym_name_callback(                 \
                       "libhookee2.so", "test_" TO_STR(inst), interceptor_##inst, NULL,            \
                       unittest_interceptor_flags, unittest_intercepted, (void *)0x123456))) {     \
        LOG("unittest: hook sym name FAILED: " TO_STR(inst) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                 \
      }                                                                                            \
    }                                                                                              \
  } while (0)

static void unittest_intercepted(int error_number, const char *lib_name, const char *sym_name, void *sym_addr,
                                 shadowhook_interceptor_t pre, void *data, void *arg) {
  LOG("unittest: intercepted callback: error_number %d, lib_name %s, sym_name %s, sym_addr %p, pre %p, data "
      "%p, arg %p",
      error_number, lib_name, sym_name, sym_addr, (void *)pre, data, arg);
}

// unintercept
#define UNINTERCEPT_WITH_TAG(inst, tag)                                                               \
  do {                                                                                                \
    if (NULL == stub_intercept_##inst##tag) break;                                                    \
    int r_ = shadowhook_unintercept(stub_intercept_##inst##tag);                                      \
    stub_intercept_##inst##tag = NULL;                                                                \
    if (0 != r_) {                                                                                    \
      if (SHADOWHOOK_ERRNO_UNHOOK_ON_UNFINISHED != shadowhook_get_errno()) {                          \
        LOG("unittest: unintercept FAILED: " TO_STR(inst##tag) ". errno %d", shadowhook_get_errno()); \
        return -1;                                                                                    \
      }                                                                                               \
    }                                                                                                 \
  } while (0)
#define UNINTERCEPT(inst) UNINTERCEPT_WITH_TAG(inst, )

// global definition for instructions
#if defined(__arm__)
GLOBL_DEF(t16_for_unique)
GLOBL_DEF(t16_for_multi)
GLOBL_DEF(t16_for_shared)

GLOBL_DEF(t16_b_t1)
GLOBL_DEF(t16_b_t1_fixaddr)
GLOBL_DEF(t16_b_t2)
GLOBL_DEF(t16_b_t2_fixaddr)
GLOBL_DEF(t16_bx_t1)
GLOBL_DEF(t16_add_reg_t2)
GLOBL_DEF(t16_mov_reg_t1)
GLOBL_DEF(t16_adr_t1)
GLOBL_DEF(t16_ldr_lit_t1)
GLOBL_DEF(t16_cbz_t1)
GLOBL_DEF(t16_cbz_t1_fixaddr)
GLOBL_DEF(t16_cbnz_t1)
GLOBL_DEF(t16_cbnz_t1_fixaddr)
GLOBL_DEF(t16_it_t1_case1)
GLOBL_DEF(t16_it_t1_case2)
GLOBL_DEF(t16_it_t1_case3)
GLOBL_DEF(t16_instr)

GLOBL_DEF(t32_b_t3)
GLOBL_DEF(t32_b_t4)
GLOBL_DEF(t32_b_t4_fixaddr)
GLOBL_DEF(t32_bl_imm_t1)
GLOBL_DEF(t32_blx_imm_t2)
GLOBL_DEF(t32_adr_t2)
GLOBL_DEF(t32_adr_t3)
GLOBL_DEF(t32_ldr_lit_t2_case1)
GLOBL_DEF(t32_ldr_lit_t2_case2)
GLOBL_DEF(t32_pld_lit_t1)
GLOBL_DEF(t32_pli_lit_t3)
GLOBL_DEF(t32_tbb_t1)
GLOBL_DEF(t32_tbh_t1)
GLOBL_DEF(t32_vldr_lit_t1_case1)
GLOBL_DEF(t32_vldr_lit_t1_case2)
GLOBL_DEF(t32_instr)

GLOBL_DEF(a32_b_a1)
GLOBL_DEF(a32_b_a1_fixaddr)
GLOBL_DEF(a32_bx_a1)
GLOBL_DEF(a32_bl_imm_a1)
GLOBL_DEF(a32_blx_imm_a2)
GLOBL_DEF(a32_add_reg_a1_case1)
GLOBL_DEF(a32_add_reg_a1_case2)
GLOBL_DEF(a32_add_reg_a1_case3)
GLOBL_DEF(a32_sub_reg_a1_case1)
GLOBL_DEF(a32_sub_reg_a1_case2)
GLOBL_DEF(a32_sub_reg_a1_case3)
GLOBL_DEF(a32_adr_a1_case1)
GLOBL_DEF(a32_adr_a1_case2)
GLOBL_DEF(a32_adr_a2_case1)
GLOBL_DEF(a32_adr_a2_case2)
GLOBL_DEF(a32_mov_reg_a1_case1)
GLOBL_DEF(a32_mov_reg_a1_case2)
GLOBL_DEF(a32_mov_reg_a1_case3)
GLOBL_DEF(a32_ldr_lit_a1_case1)
GLOBL_DEF(a32_ldr_lit_a1_case2)
GLOBL_DEF(a32_ldr_reg_a1_case1)
GLOBL_DEF(a32_ldr_reg_a1_case2)
GLOBL_DEF(a32_instr)

#elif defined(__aarch64__)

GLOBL_DEF(a64_for_unique)
GLOBL_DEF(a64_for_multi)
GLOBL_DEF(a64_for_shared)

GLOBL_DEF(a64_b)
GLOBL_DEF(a64_b_fixaddr)
GLOBL_DEF(a64_b_cond)
GLOBL_DEF(a64_b_cond_fixaddr)
GLOBL_DEF(a64_bl)
GLOBL_DEF(a64_bl_fixaddr)
GLOBL_DEF(a64_adr)
GLOBL_DEF(a64_adrp)
GLOBL_DEF(a64_ldr_lit_32)
GLOBL_DEF(a64_ldr_lit_64)
GLOBL_DEF(a64_ldrsw_lit)
GLOBL_DEF(a64_prfm_lit)
GLOBL_DEF(a64_ldr_simd_lit_32)
GLOBL_DEF(a64_ldr_simd_lit_64)
GLOBL_DEF(a64_ldr_simd_lit_128)
GLOBL_DEF(a64_cbz)
GLOBL_DEF(a64_cbz_fixaddr)
GLOBL_DEF(a64_cbnz)
GLOBL_DEF(a64_cbnz_fixaddr)
GLOBL_DEF(a64_tbz)
GLOBL_DEF(a64_tbz_fixaddr)
GLOBL_DEF(a64_tbnz)
GLOBL_DEF(a64_tbnz_fixaddr)
GLOBL_DEF(a64_instr_b)
GLOBL_DEF(a64_instr_b_cond)
GLOBL_DEF(a64_instr_cbz)
GLOBL_DEF(a64_instr_tbz)

#endif

// global definition for business logic - recursion
#define GLOBL_DEF_RECU(inst, inst2)                                    \
  static test_t orig_##inst = NULL;                                    \
  static void *stub_hook_##inst = NULL;                                \
  static int non_shared_proxy_##inst(int a, int b) {                   \
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

GLOBL_DEF_RECU(recursion_1, recursion_2)
GLOBL_DEF_RECU(recursion_2, recursion_1)

// global definition for business logic - op multiple times
GLOBL_DEF(op_multi_times_shared_1)
GLOBL_DEF(op_multi_times_shared_2)
GLOBL_DEF(op_multi_times_shared_3)
GLOBL_DEF(op_multi_times_multi_1)
GLOBL_DEF(op_multi_times_multi_2)
GLOBL_DEF(op_multi_times_multi_3)
GLOBL_DEF(op_multi_times_queue_1)
GLOBL_DEF(op_multi_times_queue_2)
GLOBL_DEF(op_multi_times_queue_3)
GLOBL_DEF(op_multi_times_queue_4)
GLOBL_DEF(op_multi_times_queue_5)
GLOBL_DEF(op_multi_times_queue_6)

// global definition for business logic - op before dlopen
GLOBL_DEF(op_before_dlopen_1)
GLOBL_DEF(op_before_dlopen_2)

// hook dlopen(), soinfo::call_constructors(), soinfo::call_destructors()
#ifndef __LP64__
#define LINKER_BASENAME "linker"
#else
#define LINKER_BASENAME "linker64"
#endif
#define SH_LINKER_SYM_CALL_CONSTRUCTORS_L "__dl__ZN6soinfo16CallConstructorsEv"
#define SH_LINKER_SYM_CALL_DESTRUCTORS_L  "__dl__ZN6soinfo15CallDestructorsEv"
#define SH_LINKER_SYM_CALL_CONSTRUCTORS_M "__dl__ZN6soinfo17call_constructorsEv"
#define SH_LINKER_SYM_CALL_DESTRUCTORS_M  "__dl__ZN6soinfo16call_destructorsEv"

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

static int hook_dlopen(void) {
  static int result = -1;
  static bool hooked = false;

  if (hooked) return result;
  hooked = true;

  void *stub =
      shadowhook_hook_sym_addr_2((void *)dlopen, (void *)linker_proxy_dlopen, (void **)&linker_orig_dlopen,
                                 SHADOWHOOK_HOOK_RECORD, LINKER_BASENAME, "dlopen");

  result = (NULL != stub && 0 == shadowhook_get_errno()) ? 0 : -1;
  return result;
}

typedef void (*linker_proxy_soinfo_call_ctors_t)(void *);
static linker_proxy_soinfo_call_ctors_t linker_orig_soinfo_call_ctors;
static void linker_proxy_soinfo_call_ctors(void *soinfo) {
  LOG("proxy_soinfo_call_ctors, soinfo %p", soinfo);
  if (SHADOWHOOK_IS_SHARED_MODE) {
    SHADOWHOOK_CALL_PREV(linker_proxy_soinfo_call_ctors, linker_proxy_soinfo_call_ctors_t, soinfo);
    SHADOWHOOK_POP_STACK();
  } else {
    linker_orig_soinfo_call_ctors(soinfo);
  }
}

typedef void (*linker_proxy_soinfo_call_dtors_t)(void *);
static linker_proxy_soinfo_call_dtors_t linker_orig_soinfo_call_dtors;
static void linker_proxy_soinfo_call_dtors(void *soinfo) {
  LOG("proxy_soinfo_call_dtors, soinfo %p", soinfo);
  if (SHADOWHOOK_IS_SHARED_MODE) {
    SHADOWHOOK_CALL_PREV(linker_proxy_soinfo_call_dtors, linker_proxy_soinfo_call_dtors_t, soinfo);
    SHADOWHOOK_POP_STACK();
  } else {
    linker_orig_soinfo_call_dtors(soinfo);
  }
}

static int hook_call_ctors_dtors(int api_level) {
  static int result = -1;
  static bool hooked = false;

  if (hooked) return result;
  hooked = true;

  void *stub_ctors = shadowhook_hook_sym_name(
      LINKER_BASENAME,
      api_level >= __ANDROID_API_M__ ? SH_LINKER_SYM_CALL_CONSTRUCTORS_M : SH_LINKER_SYM_CALL_CONSTRUCTORS_L,
      (void *)linker_proxy_soinfo_call_ctors, (void **)&linker_orig_soinfo_call_ctors);
  int errno_ctors = shadowhook_get_errno();
  void *stub_dtors = shadowhook_hook_sym_name(
      LINKER_BASENAME,
      api_level >= __ANDROID_API_M__ ? SH_LINKER_SYM_CALL_DESTRUCTORS_M : SH_LINKER_SYM_CALL_DESTRUCTORS_L,
      (void *)linker_proxy_soinfo_call_dtors, (void **)&linker_orig_soinfo_call_dtors);
  int errno_dtors = shadowhook_get_errno();

  result = (NULL != stub_ctors && NULL != stub_dtors && 0 == errno_ctors && 0 == errno_dtors) ? 0 : -1;
  return result;
}

// hidden function (without symbol info in ELF)
static test_t test_hidden_func = NULL;
GLOBL_DEF(hidden_func)

static void run_hidden_func(void) {
  if (NULL == test_hidden_func) test_hidden_func = (test_t)get_hidden_func_addr();
  RUN(hidden_func);
}

static int hook_hidden_func(void) {
  if (NULL == test_hidden_func) test_hidden_func = (test_t)get_hidden_func_addr();
  if (NULL != stub_hook_hidden_func) return -1;
  stub_hook_hidden_func = shadowhook_hook_func_addr_2(
      (void *)test_hidden_func,
      SHADOWHOOK_IS_SHARED_MODE ? (void *)shared_proxy_hidden_func : (void *)non_shared_proxy_hidden_func,
      (void **)(&orig_hidden_func), SHADOWHOOK_HOOK_RECORD, "libunittest.so", "test_hidden_func");
  return NULL == stub_hook_hidden_func ? -1 : 0;
}

static int intercept_hidden_func(void) {
  if (NULL == test_hidden_func) test_hidden_func = (test_t)get_hidden_func_addr();
  if (NULL != stub_intercept_hidden_func) return -1;
  stub_intercept_hidden_func = shadowhook_intercept_func_addr(
      (void *)test_hidden_func, interceptor_hidden_func, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_hidden_func");
  return NULL == stub_intercept_hidden_func ? -1 : 0;
}

static int intercept_instr(void) {
#if defined(__arm__)
  if (NULL != stub_intercept_t16_instr) return -1;
  stub_intercept_t16_instr = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_t16_instr + 8), interceptor_t16_instr, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_t16_instr+8");
  if (NULL == stub_intercept_t16_instr) return -1;

  if (NULL != stub_intercept_t32_instr) return -1;
  stub_intercept_t32_instr = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_t32_instr + 8), interceptor_t32_instr, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_t32_instr+8");
  if (NULL == stub_intercept_t32_instr) return -1;

  if (NULL != stub_intercept_a32_instr) return -1;
  stub_intercept_a32_instr = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_a32_instr + 8), interceptor_a32_instr, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_a32_instr+8");
  if (NULL == stub_intercept_a32_instr) return -1;

  return 0;

#elif defined(__aarch64__)
  if (NULL != stub_intercept_a64_instr_b) return -1;
  stub_intercept_a64_instr_b = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_a64_instr_b + 8), interceptor_a64_instr_b, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_a64_instr_b+8");
  if (NULL == stub_intercept_a64_instr_b) return -1;

  if (NULL != stub_intercept_a64_instr_b_cond) return -1;
  stub_intercept_a64_instr_b_cond = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_a64_instr_b_cond + 8), interceptor_a64_instr_b_cond, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_a64_instr_b_cond+8");
  if (NULL == stub_intercept_a64_instr_b_cond) return -1;

  if (NULL != stub_intercept_a64_instr_cbz) return -1;
  stub_intercept_a64_instr_cbz = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_a64_instr_cbz + 8), interceptor_a64_instr_cbz, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_a64_instr_cbz+8");
  if (NULL == stub_intercept_a64_instr_cbz) return -1;

  if (NULL != stub_intercept_a64_instr_tbz) return -1;
  stub_intercept_a64_instr_tbz = shadowhook_intercept_instr_addr(
      (void *)((uintptr_t)test_a64_instr_tbz + 8), interceptor_a64_instr_tbz, NULL,
      unittest_interceptor_flags | SHADOWHOOK_INTERCEPT_RECORD, "libunittest.so", "test_a64_instr_tbz+8");
  if (NULL == stub_intercept_a64_instr_tbz) return -1;

  return 0;
#endif
}

// test hook
static int unittest_hook(int api_level) {
  (void)api_level;
  //  if (api_level < __ANDROID_API_L__) {
  //    if (0 != hook_dlopen()) {
  //      LOG("hook dlopen() / dl_dlopen() FAILED");
  //      return -1;
  //    }
  //  } else {
  //    if (0 != hook_call_ctors_dtors(api_level)) {
  //      LOG("hook soinfo::call_constructors() and get soinfo::call_destructors() FAILED");
  //      return -1;
  //    }
  //  }

#if defined(__arm__)
  HOOK_UNIQUE(t16_for_unique);
  HOOK_MULTI(t16_for_multi);
  HOOK_SHARED(t16_for_shared);
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
  HOOK(t16_it_t1_case3);
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
  HOOK_UNIQUE(a64_for_unique);
  HOOK_MULTI(a64_for_multi);
  HOOK_SHARED(a64_for_shared);
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

  HOOK_SHARED(recursion_1);
  HOOK_SHARED(recursion_2);

  HOOK_SHARED_WITH_TAG(op_multi_times_shared, _1);
  HOOK_SHARED_WITH_TAG(op_multi_times_shared, _2);
  HOOK_SHARED_WITH_TAG(op_multi_times_shared, _3);

  HOOK_MULTI_WITH_TAG(op_multi_times_multi, _1);
  HOOK_MULTI_WITH_TAG(op_multi_times_multi, _2);
  HOOK_MULTI_WITH_TAG(op_multi_times_multi, _3);

  // multi:  1,3,6
  // shared: 2,4,5
  // queue:  1,5,4,2,3,6
  HOOK_MULTI_WITH_TAG(op_multi_times_queue, _1);
  HOOK_SHARED_WITH_TAG(op_multi_times_queue, _2);
  HOOK_MULTI_WITH_TAG(op_multi_times_queue, _3);
  HOOK_SHARED_WITH_TAG(op_multi_times_queue, _4);
  HOOK_SHARED_WITH_TAG(op_multi_times_queue, _5);
  HOOK_MULTI_WITH_TAG(op_multi_times_queue, _6);

  HOOK_PENDING(op_before_dlopen_1);
  HOOK_PENDING(op_before_dlopen_2);

  return 0;
}

static const char *unittest_get_default_mode(void) {
  if (SHADOWHOOK_IS_SHARED_MODE)
    return "SHARED";
  else if (SHADOWHOOK_IS_UNIQUE_MODE)
    return "UNIQUE";
  else if (SHADOWHOOK_IS_MULTI_MODE)
    return "MULTI";
  else
    return "UNKNOWN";
}

int unittest_hook_sym_addr(int api_level) {
  LOG("*** UNIT TEST: hook symbol/func address (default mode %s) ***", unittest_get_default_mode());
  unittest_is_hook_addr = true;
  return unittest_hook(api_level);
}

int unittest_hook_sym_name(int api_level) {
  LOG("*** UNIT TEST: hook symbol name (default mode %s) ***", unittest_get_default_mode());
  unittest_is_hook_addr = false;
  return unittest_hook(api_level);
}

// test unhook
int unittest_unhook(void) {
  LOG("*** UNIT TEST: unhook (default mode %s) ***", unittest_get_default_mode());

#if defined(__arm__)
  UNHOOK(t16_for_unique);
  UNHOOK(t16_for_multi);
  UNHOOK(t16_for_shared);
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
  UNHOOK(t16_it_t1_case3);
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
  UNHOOK(a64_for_unique);
  UNHOOK(a64_for_multi);
  UNHOOK(a64_for_shared);
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

  UNHOOK(recursion_1);
  UNHOOK(recursion_2);

  UNHOOK_WITH_TAG(op_multi_times_shared, _2);
  UNHOOK_WITH_TAG(op_multi_times_shared, _3);
  UNHOOK_WITH_TAG(op_multi_times_shared, _1);

  UNHOOK_WITH_TAG(op_multi_times_multi, _2);
  UNHOOK_WITH_TAG(op_multi_times_multi, _1);
  UNHOOK_WITH_TAG(op_multi_times_multi, _3);

  // multi:  1,3,6
  // shared: 2,4,5
  UNHOOK_WITH_TAG(op_multi_times_queue, _1);
  UNHOOK_WITH_TAG(op_multi_times_queue, _2);
  UNHOOK_WITH_TAG(op_multi_times_queue, _3);
  UNHOOK_WITH_TAG(op_multi_times_queue, _4);
  UNHOOK_WITH_TAG(op_multi_times_queue, _5);
  UNHOOK_WITH_TAG(op_multi_times_queue, _6);

  UNHOOK(op_before_dlopen_1);
  UNHOOK(op_before_dlopen_2);

  return 0;
}

// test intercept
static int unittest_intercept(void) {
#if defined(__arm__)
  INTERCEPT(t16_for_unique);
  INTERCEPT(t16_for_multi);
  INTERCEPT(t16_for_shared);
  INTERCEPT(t16_b_t1);
  INTERCEPT(t16_b_t1_fixaddr);
  INTERCEPT(t16_b_t2);
  INTERCEPT(t16_b_t2_fixaddr);
  INTERCEPT(t16_bx_t1);
  INTERCEPT(t16_add_reg_t2);
  INTERCEPT(t16_mov_reg_t1);
  INTERCEPT(t16_adr_t1);
  INTERCEPT(t16_ldr_lit_t1);
  INTERCEPT(t16_cbz_t1);
  INTERCEPT(t16_cbz_t1_fixaddr);
  INTERCEPT(t16_cbnz_t1);
  INTERCEPT(t16_cbnz_t1_fixaddr);
  INTERCEPT(t16_it_t1_case1);
  INTERCEPT(t16_it_t1_case2);
  INTERCEPT(t16_it_t1_case3);
  INTERCEPT(t32_b_t3);
  INTERCEPT(t32_b_t4);
  INTERCEPT(t32_b_t4_fixaddr);
  INTERCEPT(t32_bl_imm_t1);
  INTERCEPT(t32_blx_imm_t2);
  INTERCEPT(t32_adr_t2);
  INTERCEPT(t32_adr_t3);
  INTERCEPT(t32_ldr_lit_t2_case1);
  INTERCEPT(t32_ldr_lit_t2_case2);
  INTERCEPT(t32_pld_lit_t1);
  INTERCEPT(t32_pli_lit_t3);
  INTERCEPT(t32_tbb_t1);
  INTERCEPT(t32_tbh_t1);
  INTERCEPT(t32_vldr_lit_t1_case1);
  INTERCEPT(t32_vldr_lit_t1_case2);
  INTERCEPT(a32_b_a1);
  INTERCEPT(a32_b_a1_fixaddr);
  INTERCEPT(a32_bx_a1);
  INTERCEPT(a32_bl_imm_a1);
  INTERCEPT(a32_blx_imm_a2);
  INTERCEPT(a32_add_reg_a1_case1);
  INTERCEPT(a32_add_reg_a1_case2);
  INTERCEPT(a32_add_reg_a1_case3);
  INTERCEPT(a32_sub_reg_a1_case1);
  INTERCEPT(a32_sub_reg_a1_case2);
  INTERCEPT(a32_sub_reg_a1_case3);
  INTERCEPT(a32_adr_a1_case1);
  INTERCEPT(a32_adr_a1_case2);
  INTERCEPT(a32_adr_a2_case1);
  INTERCEPT(a32_adr_a2_case2);
  INTERCEPT(a32_mov_reg_a1_case1);
  INTERCEPT(a32_mov_reg_a1_case2);
  INTERCEPT(a32_mov_reg_a1_case3);
  INTERCEPT(a32_ldr_lit_a1_case1);
  INTERCEPT(a32_ldr_lit_a1_case2);
  INTERCEPT(a32_ldr_reg_a1_case1);
  INTERCEPT(a32_ldr_reg_a1_case2);
#elif defined(__aarch64__)
  INTERCEPT(a64_for_unique);
  INTERCEPT(a64_for_multi);
  INTERCEPT(a64_for_shared);
  INTERCEPT(a64_b);
  INTERCEPT(a64_b_fixaddr);
  INTERCEPT(a64_b_cond);
  INTERCEPT(a64_b_cond_fixaddr);
  INTERCEPT(a64_bl);
  INTERCEPT(a64_bl_fixaddr);
  INTERCEPT(a64_adr);
  INTERCEPT(a64_adrp);
  INTERCEPT(a64_ldr_lit_32);
  INTERCEPT(a64_ldr_lit_64);
  INTERCEPT(a64_ldrsw_lit);
  INTERCEPT(a64_prfm_lit);
  INTERCEPT(a64_ldr_simd_lit_32);
  INTERCEPT(a64_ldr_simd_lit_64);
  INTERCEPT(a64_ldr_simd_lit_128);
  INTERCEPT(a64_cbz);
  INTERCEPT(a64_cbz_fixaddr);
  INTERCEPT(a64_cbnz);
  INTERCEPT(a64_cbnz_fixaddr);
  INTERCEPT(a64_tbz);
  INTERCEPT(a64_tbz_fixaddr);
  INTERCEPT(a64_tbnz);
  INTERCEPT(a64_tbnz_fixaddr);
#endif

  if (unittest_is_intercept_addr) {
    if (0 != intercept_hidden_func()) {
      LOG("intercept hidden function FAILED");
      return -1;
    }
    if (0 != intercept_instr()) {
      LOG("intercept instruction FAILED");
      return -1;
    }
  }

  INTERCEPT_WITH_TAG(op_multi_times_shared, _1);
  INTERCEPT_WITH_TAG(op_multi_times_shared, _2);
  INTERCEPT_WITH_TAG(op_multi_times_shared, _3);

  INTERCEPT_WITH_TAG(op_multi_times_multi, _1);
  INTERCEPT_WITH_TAG(op_multi_times_multi, _2);
  INTERCEPT_WITH_TAG(op_multi_times_multi, _3);

  INTERCEPT_WITH_TAG(op_multi_times_queue, _1);
  INTERCEPT_WITH_TAG(op_multi_times_queue, _2);
  INTERCEPT_WITH_TAG(op_multi_times_queue, _3);
  INTERCEPT_WITH_TAG(op_multi_times_queue, _4);
  INTERCEPT_WITH_TAG(op_multi_times_queue, _5);
  INTERCEPT_WITH_TAG(op_multi_times_queue, _6);

  INTERCEPT2(op_before_dlopen_1);
  INTERCEPT2(op_before_dlopen_2);

  return 0;
}

int unittest_intercept_sym_addr(void) {
  LOG("*** UNIT TEST: intercept sym/func/instr addr (default mode %s) ***", unittest_get_default_mode());
  unittest_is_intercept_addr = true;
  unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_DEFAULT;
  return unittest_intercept();
}

int unittest_intercept_sym_addr_read_fpsimd(void) {
  LOG("*** UNIT TEST: intercept(r FPSIMD) sym/func/instr addr (default mode %s) ***",
      unittest_get_default_mode());
  unittest_is_intercept_addr = true;
  unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_ONLY;
  return unittest_intercept();
}

int unittest_intercept_sym_addr_read_write_fpsimd(void) {
  LOG("*** UNIT TEST: intercept(rw FPSIMD) sym/func/instr addr (default mode %s) ***",
      unittest_get_default_mode());
  unittest_is_intercept_addr = true;
  unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_WRITE;
  return unittest_intercept();
}

int unittest_intercept_sym_name(void) {
  LOG("*** UNIT TEST: intercept sym name (default mode %s) ***", unittest_get_default_mode());
  unittest_is_intercept_addr = false;
  unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_DEFAULT;
  return unittest_intercept();
}

int unittest_intercept_sym_name_read_fpsimd(void) {
  LOG("*** UNIT TEST: intercept(r FPSIMD) sym name (default mode %s) ***", unittest_get_default_mode());
  unittest_is_intercept_addr = false;
  unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_ONLY;
  return unittest_intercept();
}

int unittest_intercept_sym_name_read_write_fpsimd(void) {
  LOG("*** UNIT TEST: intercept(rw FPSIMD) sym name (default mode %s) ***", unittest_get_default_mode());
  unittest_is_intercept_addr = false;
  unittest_interceptor_flags = SHADOWHOOK_INTERCEPT_WITH_FPSIMD_READ_WRITE;
  return unittest_intercept();
}

int unittest_unintercept(void) {
  LOG("*** UNIT TEST: unintercept (default mode %s) ***", unittest_get_default_mode());

#if defined(__arm__)
  UNINTERCEPT(t16_for_unique);
  UNINTERCEPT(t16_for_multi);
  UNINTERCEPT(t16_for_shared);
  UNINTERCEPT(t16_b_t1);
  UNINTERCEPT(t16_b_t1_fixaddr);
  UNINTERCEPT(t16_b_t2);
  UNINTERCEPT(t16_b_t2_fixaddr);
  UNINTERCEPT(t16_bx_t1);
  UNINTERCEPT(t16_add_reg_t2);
  UNINTERCEPT(t16_mov_reg_t1);
  UNINTERCEPT(t16_adr_t1);
  UNINTERCEPT(t16_ldr_lit_t1);
  UNINTERCEPT(t16_cbz_t1);
  UNINTERCEPT(t16_cbz_t1_fixaddr);
  UNINTERCEPT(t16_cbnz_t1);
  UNINTERCEPT(t16_cbnz_t1_fixaddr);
  UNINTERCEPT(t16_it_t1_case1);
  UNINTERCEPT(t16_it_t1_case2);
  UNINTERCEPT(t16_it_t1_case3);
  UNINTERCEPT(t32_b_t3);
  UNINTERCEPT(t32_b_t4);
  UNINTERCEPT(t32_b_t4_fixaddr);
  UNINTERCEPT(t32_bl_imm_t1);
  UNINTERCEPT(t32_blx_imm_t2);
  UNINTERCEPT(t32_adr_t2);
  UNINTERCEPT(t32_adr_t3);
  UNINTERCEPT(t32_ldr_lit_t2_case1);
  UNINTERCEPT(t32_ldr_lit_t2_case2);
  UNINTERCEPT(t32_pld_lit_t1);
  UNINTERCEPT(t32_pli_lit_t3);
  UNINTERCEPT(t32_tbb_t1);
  UNINTERCEPT(t32_tbh_t1);
  UNINTERCEPT(t32_vldr_lit_t1_case1);
  UNINTERCEPT(t32_vldr_lit_t1_case2);
  UNINTERCEPT(a32_b_a1);
  UNINTERCEPT(a32_b_a1_fixaddr);
  UNINTERCEPT(a32_bx_a1);
  UNINTERCEPT(a32_bl_imm_a1);
  UNINTERCEPT(a32_blx_imm_a2);
  UNINTERCEPT(a32_add_reg_a1_case1);
  UNINTERCEPT(a32_add_reg_a1_case2);
  UNINTERCEPT(a32_add_reg_a1_case3);
  UNINTERCEPT(a32_sub_reg_a1_case1);
  UNINTERCEPT(a32_sub_reg_a1_case2);
  UNINTERCEPT(a32_sub_reg_a1_case3);
  UNINTERCEPT(a32_adr_a1_case1);
  UNINTERCEPT(a32_adr_a1_case2);
  UNINTERCEPT(a32_adr_a2_case1);
  UNINTERCEPT(a32_adr_a2_case2);
  UNINTERCEPT(a32_mov_reg_a1_case1);
  UNINTERCEPT(a32_mov_reg_a1_case2);
  UNINTERCEPT(a32_mov_reg_a1_case3);
  UNINTERCEPT(a32_ldr_lit_a1_case1);
  UNINTERCEPT(a32_ldr_lit_a1_case2);
  UNINTERCEPT(a32_ldr_reg_a1_case1);
  UNINTERCEPT(a32_ldr_reg_a1_case2);
  UNINTERCEPT(t16_instr);
  UNINTERCEPT(t32_instr);
  UNINTERCEPT(a32_instr);
#elif defined(__aarch64__)
  UNINTERCEPT(a64_for_unique);
  UNINTERCEPT(a64_for_multi);
  UNINTERCEPT(a64_for_shared);
  UNINTERCEPT(a64_b);
  UNINTERCEPT(a64_b_fixaddr);
  UNINTERCEPT(a64_b_cond);
  UNINTERCEPT(a64_b_cond_fixaddr);
  UNINTERCEPT(a64_bl);
  UNINTERCEPT(a64_bl_fixaddr);
  UNINTERCEPT(a64_adr);
  UNINTERCEPT(a64_adrp);
  UNINTERCEPT(a64_ldr_lit_32);
  UNINTERCEPT(a64_ldr_lit_64);
  UNINTERCEPT(a64_ldrsw_lit);
  UNINTERCEPT(a64_prfm_lit);
  UNINTERCEPT(a64_ldr_simd_lit_32);
  UNINTERCEPT(a64_ldr_simd_lit_64);
  UNINTERCEPT(a64_ldr_simd_lit_128);
  UNINTERCEPT(a64_cbz);
  UNINTERCEPT(a64_cbz_fixaddr);
  UNINTERCEPT(a64_cbnz);
  UNINTERCEPT(a64_cbnz_fixaddr);
  UNINTERCEPT(a64_tbz);
  UNINTERCEPT(a64_tbz_fixaddr);
  UNINTERCEPT(a64_tbnz);
  UNINTERCEPT(a64_tbnz_fixaddr);
  UNINTERCEPT(a64_instr_b);
  UNINTERCEPT(a64_instr_b_cond);
  UNINTERCEPT(a64_instr_cbz);
  UNINTERCEPT(a64_instr_tbz);
#endif

  UNINTERCEPT(hidden_func);

  UNINTERCEPT_WITH_TAG(op_multi_times_shared, _2);
  UNINTERCEPT_WITH_TAG(op_multi_times_shared, _3);
  UNINTERCEPT_WITH_TAG(op_multi_times_shared, _1);

  UNINTERCEPT_WITH_TAG(op_multi_times_multi, _2);
  UNINTERCEPT_WITH_TAG(op_multi_times_multi, _3);
  UNINTERCEPT_WITH_TAG(op_multi_times_multi, _1);

  UNINTERCEPT_WITH_TAG(op_multi_times_queue, _2);
  UNINTERCEPT_WITH_TAG(op_multi_times_queue, _3);
  UNINTERCEPT_WITH_TAG(op_multi_times_queue, _1);
  UNINTERCEPT_WITH_TAG(op_multi_times_queue, _6);
  UNINTERCEPT_WITH_TAG(op_multi_times_queue, _4);
  UNINTERCEPT_WITH_TAG(op_multi_times_queue, _5);

  UNINTERCEPT(op_before_dlopen_1);
  UNINTERCEPT(op_before_dlopen_2);

  return 0;
}

int unittest_run(bool hookee2_loaded) {
  LOG("*** UNIT TEST: run (default mode %s) ***", unittest_get_default_mode());

#if defined(__arm__)

  LOG(DELIMITER, "TEST INST T16");
  RUN(t16_for_unique);
  RUN(t16_for_multi);
  RUN(t16_for_shared);
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
  RUN(t16_it_t1_case3);

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

  RUN(a64_for_unique);
  RUN(a64_for_multi);
  RUN(a64_for_shared);
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

  LOG(DELIMITER, "TEST - instruction interceptor");
#if defined(__arm__)
  RUN(t16_instr);
  RUN(t32_instr);
  RUN(a32_instr);
#elif defined(__aarch64__)
  LOG("NOTE: When testing hook-without-island on arm64, errors may occur. This is expected.");
  RUN(a64_instr_b);
  RUN(a64_instr_b_cond);
  RUN(a64_instr_cbz);
  RUN(a64_instr_tbz);
#endif

  LOG(DELIMITER, "TEST - recursion");
  RUN(recursion_1);
  LOG(DELIMITER, "TEST - op multi times (shared)");
  RUN(op_multi_times_shared);
  LOG(DELIMITER, "TEST - op multi times (multi)");
  RUN(op_multi_times_multi);
  LOG(DELIMITER, "TEST - op multi times (multi + shared)");
  RUN(op_multi_times_queue);

  if (hookee2_loaded) {
    LOG(DELIMITER, "TEST - op before dlopen");
    RUN_WITH_DLSYM(libhookee2.so, op_before_dlopen_1);
    RUN_WITH_DLSYM(libhookee2.so, op_before_dlopen_2);
  }

  LOG(DELIMITER, "TEST - dlopen");
  void *handle = dlopen("libc.so", RTLD_NOW);
  dlclose(handle);
  handle = dlopen("libshadowhook_nothing.so", RTLD_NOW);
  dlclose(handle);

  return 0;
}

static void unittest_set_cpu_affinity(bool big_core) {
  int cpu_num = (int)sysconf(_SC_NPROCESSORS_CONF);
  int cpu_id = big_core ? cpu_num - 1 : 0;
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_id, &cpuset);
  sched_setaffinity(0, sizeof(cpuset), &cpuset);
  LOG("set CPU affinity to CPU(%d) - %s core", cpu_id, big_core ? "BIG" : "LITTLE");
}

__attribute__((optnone)) static void unittest_cpu_warmup(void) {
  LOG("CPU warmup ......");
  for (int i = 0; i < 10; i++) {
    int s = 1;
    float n = 1.0, t = 1, pi = 0;
    while (fabs((double)t) > 3 * 1e-8) {
      pi = pi + t;
      n = n + 2;
      s = -s;
      t = (float)s / n;
    }
  }
}

static uint64_t unittest_get_usec(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000 * 1000 + (uint64_t)tv.tv_usec;
}

static void unittest_benchmark_in_mode(const char *mode, test_t test_func) {
  size_t cycles = 1000 * 10000;
  uint64_t start = unittest_get_usec();

  for (size_t i = 0; i < cycles; i++) {
    if (__predict_false(12 != test_func(4, 8))) abort();
  }

  uint64_t end = unittest_get_usec();
  LOG("%zu cycles take %" PRIu64 " us (in %s mode)", cycles, end - start, mode);
}

static void unittest_benchmark_in_core(bool big_core) {
  LOG("*** UNIT TEST: benchmark ***");
  unittest_set_cpu_affinity(big_core);
  unittest_cpu_warmup();
#if defined(__arm__)
  unittest_benchmark_in_mode("UNIQUE", test_t16_for_unique);
  unittest_benchmark_in_mode("MULTI", test_t16_for_multi);
  unittest_benchmark_in_mode("SHARED", test_t16_for_shared);
#elif defined(__aarch64__)
  unittest_benchmark_in_mode("UNIQUE", test_a64_for_unique);
  unittest_benchmark_in_mode("MULTI", test_a64_for_multi);
  unittest_benchmark_in_mode("SHARED", test_a64_for_shared);
#endif
}

int unittest_benchmark(void) {
  unittest_is_benchmark = true;
  unittest_benchmark_in_core(true);
  unittest_benchmark_in_core(false);
  unittest_is_benchmark = false;
  return 0;
}

#pragma clang diagnostic pop
