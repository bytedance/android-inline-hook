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

#include "sh_island.h"
#include "sh_linker.h"

typedef struct {
  uint8_t backup[12];    // max-length = 10 (4-byte alignment)
  size_t backup_len;     // = 4 or 8(arm); = 4 or 8 or 10(thumb)
  size_t rewritten_len;  // = backup_len(arm); >= backup_len(thumb)
  uint32_t exit[3];      // max-length = 10 (4-byte alignment), length == backup_len
  uintptr_t enter;
  sh_island_t island_exit;  // .size = 8(arm & thumb)
} sh_inst_t;

typedef void (*sh_inst_set_orig_addr_t)(uintptr_t orig_addr, void *arg);
int sh_inst_hook(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                 bool is_to_interceptor, sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg);
int sh_inst_rehook(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                   bool is_to_interceptor);
int sh_inst_unhook(sh_inst_t *self, uintptr_t target_addr);

void sh_inst_free_after_dlclose(sh_inst_t *self, uintptr_t target_addr);

void sh_inst_build_glue_launcher(void *buf, void *ctx);
