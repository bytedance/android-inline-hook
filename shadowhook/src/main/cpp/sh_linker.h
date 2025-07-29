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

#include "sh_util.h"
#include "shadowhook.h"

int sh_linker_init(void);

// for Android 4.x
#if SH_UTIL_COMPATIBLE_WITH_ARM_ANDROID_4_X
bool sh_linker_need_to_pre_register(uintptr_t target_addr);
typedef void (*sh_linker_dlopen_post_t)(void);
int sh_linker_register_dlopen_post_callback(sh_linker_dlopen_post_t post);
#endif

// for Android >= 5.0
int sh_linker_register_dl_init_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int sh_linker_unregister_dl_init_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int sh_linker_register_dl_fini_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);
int sh_linker_unregister_dl_fini_callback(shadowhook_dl_info_t pre, shadowhook_dl_info_t post, void *data);

// linker utils
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
  void *dli_fbase;              // ELF load_bias
  void *dli_saddr;              // symbol address
  size_t dli_ssize;             // symbol size
  const ElfW(Phdr) *dlpi_phdr;  // ELF program headers
  size_t dlpi_phnum;            // number of items in dlpi_phdr
  bool is_sym_addr;
  bool is_proc_start;
} sh_addr_info_t;
#pragma clang diagnostic pop
int sh_linker_get_addr_info_by_addr(void *addr, bool is_sym_addr, bool is_proc_start,
                                    sh_addr_info_t *addr_info, bool ignore_sym_info, char *fname,
                                    size_t fname_len);
int sh_linker_get_addr_info_by_sym_name(const char *lib_name, const char *sym_name,
                                        sh_addr_info_t *addr_info);
void sh_linker_get_fname_by_fbase(void *fbase, char *fname, size_t fname_len);
bool sh_linker_is_addr_in_elf_pt_load(uintptr_t addr, void *dli_fbase, const ElfW(Phdr) *dlpi_phdr,
                                      size_t dlpi_phnum);

void shadowhook_proxy_android_linker_soinfo_call_constructors(void *soinfo);
void shadowhook_proxy_android_linker_soinfo_call_destructors(void *soinfo);
