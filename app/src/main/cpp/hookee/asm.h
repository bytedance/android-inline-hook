// Copyright (c) 2021-2024 ByteDance Inc.
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

#if defined(__arm__)
#define asm_mode          .arm
#define asm_align         0
#define asm_align_bound   4
#define asm_function_type #function
#define asm_custom_entry  .fnstart
#define asm_custom_end    .fnend
#elif defined(__aarch64__)
#define asm_mode
#define asm_align         4
#define asm_align_bound   16
#define asm_function_type % function
#define asm_custom_entry
#define asm_custom_end
#endif

#define ENTRY(f)              \
  .text;                      \
  .type f, asm_function_type; \
  f:                          \
  asm_custom_entry;           \
  .cfi_startproc

#define ENTRY_GLOBAL_THUMB(f) \
  .globl f;                   \
  .thumb;                     \
  .balign asm_align;          \
  ENTRY(f)

#define ENTRY_GLOBAL_ARM(f) \
  .globl f;                 \
  asm_mode;                 \
  .balign asm_align;        \
  ENTRY(f)

#define ENTRY_HIDDEN_THUMB(f) \
  .hidden f;                  \
  .thumb;                     \
  .balign asm_align;          \
  ENTRY(f)

#define ENTRY_HIDDEN_ARM(f) \
  .hidden f;                \
  asm_mode;                 \
  .balign asm_align;        \
  ENTRY(f)

#define ENTRY_GLOBAL_THUMB_BOUND(f) \
  .globl f;                         \
  .thumb;                           \
  .balign asm_align_bound;          \
  ENTRY(f)

#define ENTRY_GLOBAL_ARM_BOUND(f) \
  .globl f;                       \
  asm_mode;                       \
  .balign asm_align_bound;        \
  ENTRY(f)

#define ENTRY_HIDDEN_THUMB_BOUND(f) \
  .hidden f;                        \
  .thumb;                           \
  .balign asm_align_bound;          \
  ENTRY(f)

#define ENTRY_HIDDEN_ARM_BOUND(f) \
  .hidden f;                      \
  asm_mode;                       \
  .balign asm_align_bound;        \
  ENTRY(f)

#define END(f)   \
  .cfi_endproc;  \
  .size f, .- f; \
  asm_custom_end\
