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

#define SH_RECORDER_OP_HOOK_INSTR_ADDR      0
#define SH_RECORDER_OP_HOOK_FUNC_ADDR       1
#define SH_RECORDER_OP_HOOK_SYM_ADDR        2
#define SH_RECORDER_OP_HOOK_SYM_NAME        3
#define SH_RECORDER_OP_UNHOOK               4
#define SH_RECORDER_OP_INTERCEPT_INSTR_ADDR 5
#define SH_RECORDER_OP_INTERCEPT_FUNC_ADDR  6
#define SH_RECORDER_OP_INTERCEPT_SYM_ADDR   7
#define SH_RECORDER_OP_INTERCEPT_SYM_NAME   8
#define SH_RECORDER_OP_UNINTERCEPT          9

bool sh_recorder_get_recordable(void);
void sh_recorder_set_recordable(bool recordable);

int sh_recorder_add_op(int error_number, uint8_t op, uintptr_t sym_addr, const char *lib_name,
                       const char *sym_name, uintptr_t new_addr, uint32_t flags, size_t backup_len,
                       uintptr_t stub, uintptr_t caller_addr, const char *caller_lib_name);
int sh_recorder_add_unop(int error_number, uint8_t op, uintptr_t stub, uintptr_t caller_addr,
                         const char *caller_lib_name);

char *sh_recorder_get(uint32_t item_flags);
void sh_recorder_dump(int fd, uint32_t item_flags);
