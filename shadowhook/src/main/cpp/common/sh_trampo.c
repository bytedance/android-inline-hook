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

#include "sh_trampo.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/time.h>

#include "queue.h"
#include "sh_util.h"

#define SH_TRAMPO_ALIGN 4

void sh_trampo_init_mgr(sh_trampo_mgr_t *mgr, const char *anon_page_name, size_t trampo_size,
                        time_t delay_sec) {
  SLIST_INIT(&mgr->pages);
  pthread_mutex_init(&mgr->pages_lock, NULL);
  mgr->anon_page_name = anon_page_name;
  mgr->trampo_size = SH_UTIL_ALIGN_END(trampo_size, SH_TRAMPO_ALIGN);
  mgr->delay_sec = delay_sec;
}

uintptr_t sh_trampo_alloc(sh_trampo_mgr_t *mgr) {
  return sh_trampo_alloc_between(mgr, 0, 0);
}

// range: [range_low, range_high]
uintptr_t sh_trampo_alloc_between(sh_trampo_mgr_t *mgr, uintptr_t range_low, uintptr_t range_high) {
  if (range_high < range_low) return 0;
  bool between = (range_low > 0 || range_high > 0);
  if (between && (range_high - range_low < mgr->trampo_size)) return 0;

  uint32_t now = (uint32_t)sh_util_get_stable_timestamp();
  size_t page_size = sh_util_get_page_size();
  size_t trampo_page_size = page_size;
  size_t trampo_count = trampo_page_size / mgr->trampo_size;
  uintptr_t trampo = 0;
  uintptr_t new_ptr = (uintptr_t)MAP_FAILED;
  uintptr_t new_ptr_prctl = (uintptr_t)MAP_FAILED;

  pthread_mutex_lock(&mgr->pages_lock);

  // try to find an unused trampo
  sh_trampo_page_t *page;
  SLIST_FOREACH(page, &mgr->pages, link) {
    // check page's range
    uintptr_t trampo_first = page->ptr;
    uintptr_t trampo_last = page->ptr + (trampo_count - 1) * mgr->trampo_size;
    if (between && ((trampo_last < range_low) || (range_high < trampo_first))) continue;

    for (uintptr_t i = 0; i < trampo_count; i++) {
      // check current trampo's range
      uintptr_t trampo_cur = page->ptr + (mgr->trampo_size * i);
      if (between && ((trampo_cur < range_low) || (range_high < trampo_cur))) continue;

      // check if used
      uint32_t used = page->flags[i] >> 31;
      if (used) continue;

      // check timestamp
      uint32_t ts = page->flags[i] & 0x7FFFFFFF;
      if (now <= ts || now - ts <= (uint32_t)mgr->delay_sec) continue;

      // OK
      page->flags[i] |= 0x80000000;
      trampo = trampo_cur;
      memset((void *)trampo, 0, mgr->trampo_size);
      goto end;
    }
  }

  // alloc a new memory page
  void *hint = between ? (void *)(sh_util_page_end(range_low)) : NULL;
  new_ptr = (uintptr_t)(mmap(hint, trampo_page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if ((uintptr_t)MAP_FAILED == new_ptr) goto err;
  new_ptr_prctl = new_ptr;

  // check page's range
  uintptr_t trampo_first = new_ptr;
  uintptr_t trampo_last = new_ptr + (trampo_count - 1) * mgr->trampo_size;
  if (between && ((trampo_last < range_low) || (range_high < trampo_first))) goto err;

  // create a new trampo-page info
  if (NULL == (page = calloc(1, sizeof(sh_trampo_page_t) + trampo_count * sizeof(uint32_t)))) goto err;
  memset((void *)new_ptr, 0, trampo_page_size);
  page->ptr = new_ptr;
  new_ptr = (uintptr_t)MAP_FAILED;
  SLIST_INSERT_HEAD(&mgr->pages, page, link);

  // alloc trampo from the new memory page
  for (uintptr_t i = 0; i < trampo_count; i++) {
    // check current trampo's range
    uintptr_t trampo_cur = page->ptr + (mgr->trampo_size * i);
    if (between && ((trampo_cur < range_low) || (range_high < trampo_cur))) continue;

    // OK
    page->flags[i] |= 0x80000000;
    trampo = trampo_cur;
    break;
  }
  if (0 == trampo) abort();

end:
  pthread_mutex_unlock(&mgr->pages_lock);
  if ((uintptr_t)MAP_FAILED != new_ptr_prctl)
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, new_ptr_prctl, trampo_page_size, mgr->anon_page_name);
  return trampo;

err:
  pthread_mutex_unlock(&mgr->pages_lock);
  if (NULL != page) {
    if (0 != page->ptr) munmap((void *)page->ptr, trampo_page_size);
    free(page);
  }
  if ((uintptr_t)MAP_FAILED != new_ptr) munmap((void *)new_ptr, trampo_page_size);
  return 0;
}

void sh_trampo_free(sh_trampo_mgr_t *mgr, uintptr_t trampo) {
  time_t now = sh_util_get_stable_timestamp();
  size_t trampo_page_size = sh_util_get_page_size();
  size_t trampo_count = trampo_page_size / mgr->trampo_size;

  pthread_mutex_lock(&mgr->pages_lock);

  sh_trampo_page_t *page;
  SLIST_FOREACH(page, &mgr->pages, link) {
    // check page's hit range: [page_start, page_end)
    uintptr_t page_start = page->ptr;
    uintptr_t page_end = page->ptr + trampo_count * mgr->trampo_size;
    if (page_start <= trampo && trampo < page_end) {
      uintptr_t i = (trampo - page_start) / mgr->trampo_size;
      page->flags[i] = now & 0x7FFFFFFF;
      break;
    }
  }

  pthread_mutex_unlock(&mgr->pages_lock);
}
