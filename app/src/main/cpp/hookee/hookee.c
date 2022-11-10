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

#include "hookee.h"

#include <android/log.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#define LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, "shadowhook_tag", fmt, ##__VA_ARGS__)
#pragma clang diagnostic pop

static uint8_t bss[4196];
static uint8_t data[4196] = {1};

static void hookee_init_helper(void) {
  for (size_t i = 0; i < sizeof(data); i++) data[i] = 10;
  for (size_t i = 0; i < sizeof(bss); i++) bss[i] = 11;
}

#if defined(__arm__)

static int hookee_fill(uintptr_t addr, uintptr_t val) {
  uintptr_t aligned_addr = (addr & ~((uintptr_t)(PAGE_SIZE - 1)));
  size_t aligned_len = (1 + ((addr + sizeof(uintptr_t) - 1 - aligned_addr) / PAGE_SIZE)) * PAGE_SIZE;

  if (0 != mprotect((void *)aligned_addr, aligned_len, PROT_READ | PROT_WRITE | PROT_EXEC)) {
    LOG("hookee_edit failed: %" PRIxPTR, addr);
    return -1;
  }
  *((uintptr_t *)addr) = val;
  __builtin___clear_cache((char *)addr, (char *)(addr + sizeof(uintptr_t)));
  mprotect((void *)aligned_addr, aligned_len, PROT_READ | PROT_EXEC);

  return 0;
}

__attribute__((constructor)) static void hookee_init(void) {
  hookee_init_helper();

  hookee_fill((uintptr_t)test_t32_ldr_lit_t2_case2 + 8 - 1, (uintptr_t)test_t32_helper_global);
  hookee_fill((uintptr_t)test_a32_ldr_lit_a1_case2 + 12, (uintptr_t)test_a32_helper_global);
  hookee_fill((uintptr_t)test_a32_ldr_reg_a1_case2 + 20, (uintptr_t)test_a32_helper_global);
}

#elif defined(__aarch64__)

__attribute__((constructor)) static void hookee_init(void) {
  hookee_init_helper();
}

#endif

int test_recursion_1(int a, int b) {
  LOG("**> test_recursion_1 called");
  return a + b;
}

int test_recursion_2(int a, int b) {
  LOG("**> test_recursion_2 called");
  return a + b;
}

int test_hook_multi_times(int a, int b) {
  LOG("**> test_hook_multiple_times called");
  return a + b;
}

void *get_hidden_func_addr(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpointer-arith"
#if defined(__arm__)
  return (void *)test_a32_helper_global + 8;
#elif defined(__aarch64__)
  return (void *)test_a64_helper_global + 8;
#endif
#pragma clang diagnostic pop
}
