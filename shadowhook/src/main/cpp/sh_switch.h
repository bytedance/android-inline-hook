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

#include "sh_linker.h"
#include "shadowhook.h"

void shadowhook_interceptor_caller(void *ctx, shadowhook_cpu_context_t *cpu_context, void **next_hop);

void sh_switch_init(void);

int sh_switch_hook(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr, uintptr_t *orig_addr,
                   size_t flags, size_t *backup_len);
int sh_switch_unhook(uintptr_t target_addr, uintptr_t new_addr, size_t flags);

int sh_switch_hook_invisible(uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                             uintptr_t *orig_addr, size_t *backup_len);

int sh_switch_intercept(uintptr_t target_addr, sh_addr_info_t *addr_info, shadowhook_interceptor_t pre,
                        void *data, size_t flags, size_t *backup_len);
int sh_switch_unintercept(uintptr_t target_addr, shadowhook_interceptor_t pre, void *data);

void sh_switch_free_after_dlclose(struct dl_phdr_info *info);
