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

#include "sh_island.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sh_elf.h"
#include "sh_log.h"
#include "sh_trampo.h"

#define SH_ISLAND_TYPE_ANON_PAGE 0
#define SH_ISLAND_TYPE_ELF_GAP   1
// add more island type here ......

#define SH_ISLAND_ANON_PAGE_NAME "shadowhook-island"
#define SH_ISLAND_DELAY_SEC      3
#if defined(__arm__)
#define SH_ISLAND_SIZE_MAX 8
#elif defined(__aarch64__)
#define SH_ISLAND_SIZE_MAX 20
#endif

static sh_trampo_mgr_t sh_island_trampo_mgr;

void sh_island_init(void) {
  sh_trampo_init_mgr(&sh_island_trampo_mgr, SH_ISLAND_ANON_PAGE_NAME, SH_ISLAND_SIZE_MAX,
                     SH_ISLAND_DELAY_SEC);
}

// range: [range_low, range_high]
void sh_island_alloc(sh_island_t *self, size_t size, uintptr_t range_low, uintptr_t range_high, uintptr_t pc,
                     sh_addr_info_t *addr_info) {
  self->size = size;

  // try to allocate via mmap()
  self->type = SH_ISLAND_TYPE_ANON_PAGE;
  self->addr = sh_trampo_alloc_between(&sh_island_trampo_mgr, range_low, range_high);
  if (0 != self->addr) goto ok;

  // try to allocate in ELF gaps
  self->type = SH_ISLAND_TYPE_ELF_GAP;
  self->addr = sh_elf_alloc(size, range_low, range_high, pc, addr_info);
  if (0 != self->addr) goto ok;

  return;

ok:
  SH_LOG_INFO("island: alloc %s, addr %" PRIxPTR ", size %zu, pc %" PRIxPTR ", range [%zx, %zx]",
              (self->type == SH_ISLAND_TYPE_ELF_GAP ? "in ELF-gap" : "via anon-page"), self->addr, size, pc,
              range_low, range_high);
}

void sh_island_free(sh_island_t *self) {
  if (0 == self->addr) return;

  if (SH_ISLAND_TYPE_ANON_PAGE == self->type) {
    sh_trampo_free(&sh_island_trampo_mgr, self->addr);
  } else if (SH_ISLAND_TYPE_ELF_GAP == self->type) {
    sh_elf_free(self->addr, self->size);
  }
  self->addr = 0;
}

void sh_island_free_after_dlclose(sh_island_t *self) {
  if (SH_ISLAND_TYPE_ANON_PAGE == self->type && 0 != self->addr) {
    sh_trampo_free(&sh_island_trampo_mgr, self->addr);
  }
}

void sh_island_cleanup_after_dlclose(uintptr_t load_bias) {
  sh_elf_cleanup_after_dlclose(load_bias);
}
