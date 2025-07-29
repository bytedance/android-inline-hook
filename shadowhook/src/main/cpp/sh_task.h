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

#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "shadowhook.h"

typedef struct sh_task sh_task_t;

int sh_task_init(void);

sh_task_t *sh_task_create_hook_by_target_addr(uintptr_t target_addr, uintptr_t new_addr, uintptr_t *orig_addr,
                                              uint32_t flags, bool is_sym_addr, bool is_proc_start,
                                              uintptr_t caller_addr, char *record_lib_name,
                                              char *record_sym_name);
sh_task_t *sh_task_create_hook_by_sym_name(const char *lib_name, const char *sym_name, uintptr_t new_addr,
                                           uintptr_t *orig_addr, uint32_t flags, shadowhook_hooked_t hooked,
                                           void *hooked_arg, uintptr_t caller_addr);

sh_task_t *sh_task_create_intercept_by_target_addr(uintptr_t target_addr, shadowhook_interceptor_t pre,
                                                   void *data, uint32_t flags, bool is_sym_addr,
                                                   bool is_proc_start, uintptr_t caller_addr,
                                                   char *record_lib_name, char *record_sym_name);
sh_task_t *sh_task_create_intercept_by_sym_name(const char *lib_name, const char *sym_name,
                                                shadowhook_interceptor_t pre, void *data, uint32_t flags,
                                                shadowhook_intercepted_t intercepted, void *intercepted_arg,
                                                uintptr_t caller_addr);

void sh_task_destroy(sh_task_t *self);

int sh_task_do(sh_task_t *self);
int sh_task_undo(sh_task_t *self, uintptr_t caller_addr);
