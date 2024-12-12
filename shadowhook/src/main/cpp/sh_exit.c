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

#include "sh_exit.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "sh_config.h"
#include "sh_linker.h"
#include "sh_log.h"
#include "sh_sig.h"
#include "sh_trampo.h"
#include "sh_util.h"
#include "shadowhook.h"
#include "xdl.h"

#define SH_EXIT_TYPE_OUT_LIBRARY 0
#define SH_EXIT_TYPE_IN_LIBRARY  1

#define SH_EXIT_PAGE_NAME "shadowhook-exit"
#define SH_EXIT_DELAY_SEC 3
#if defined(__arm__)
#define SH_EXIT_SZ 8
#elif defined(__aarch64__)
#define SH_EXIT_SZ 16
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-statement-expression"

//
// (1) out-library mode:
//
// We store the shellcode for exit in newly mmaped memory near the PC.
//
static sh_trampo_mgr_t sh_exit_trampo_mgr;

static void sh_exit_init_out_library(void) {
  sh_trampo_init_mgr(&sh_exit_trampo_mgr, SH_EXIT_PAGE_NAME, SH_EXIT_SZ, SH_EXIT_DELAY_SEC);
}

static int sh_exit_alloc_out_library(uintptr_t *exit_addr, uintptr_t pc, uint8_t *exit, size_t range_low,
                                     size_t range_high) {
  uintptr_t addr = sh_trampo_alloc_near(&sh_exit_trampo_mgr, pc, range_low, range_high);
  if (0 == addr) return -1;

  memcpy((void *)addr, exit, SH_EXIT_SZ);
  sh_util_clear_cache(addr, SH_EXIT_SZ);
  *exit_addr = addr;
  return 0;
}

static void sh_exit_free_out_library(uintptr_t exit_addr) {
  sh_trampo_free(&sh_exit_trampo_mgr, exit_addr);
}

//
// (2) in-library mode:
//
// We store the shellcode for exit in the memory gaps in the current ELF.
//

// ELF gap, range: [start, end)
typedef struct {
  uintptr_t start;
  uintptr_t end;
  uint32_t *flags;  // flags for each trampo: 1 bit for used/unused, 31 bits for timestamp
} sh_exit_elf_gap_t;

// ELF info
typedef struct sh_exit_elf_info {
  void *dli_fbase;
  const ElfW(Phdr) *dlpi_phdr;
  size_t dlpi_phnum;
  sh_exit_elf_gap_t *gaps;
  size_t gaps_num;
  TAILQ_ENTRY(sh_exit_elf_info, ) link;
} sh_exit_elf_info_t;
typedef TAILQ_HEAD(sh_exit_elf_info_list, sh_exit_elf_info, ) sh_exit_elf_info_list_t;

// ELF info list
static sh_exit_elf_info_list_t sh_exit_elf_infos = TAILQ_HEAD_INITIALIZER(sh_exit_elf_infos);
static pthread_mutex_t sh_exit_elf_infos_lock = PTHREAD_MUTEX_INITIALIZER;

static uintptr_t sh_exit_exec_load_bias;
static uintptr_t sh_exit_linker_load_bias;
static uintptr_t sh_exit_vdso_load_bias;

extern __attribute((weak)) unsigned long int getauxval(unsigned long int);

static uintptr_t sh_exit_get_load_bias_from_aux(unsigned long type) {
  if (__predict_false(NULL == getauxval)) return 0;

  uintptr_t val = (uintptr_t)getauxval(type);
  if (__predict_false(0 == val)) return 0;

  // get base
  uintptr_t base = (AT_PHDR == type ? (val & (~0xffful)) : val);
  if (__predict_false(0 != memcmp((void *)base, ELFMAG, SELFMAG))) return 0;

  // ELF info
  ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)base;
  const ElfW(Phdr) *dlpi_phdr = (const ElfW(Phdr) *)(base + ehdr->e_phoff);
  ElfW(Half) dlpi_phnum = ehdr->e_phnum;

  // get load_bias
  uintptr_t min_vaddr = UINTPTR_MAX;
  for (size_t i = 0; i < dlpi_phnum; i++) {
    const ElfW(Phdr) *phdr = &(dlpi_phdr[i]);
    if (PT_LOAD == phdr->p_type) {
      if (min_vaddr > phdr->p_vaddr) min_vaddr = phdr->p_vaddr;
    }
  }
  if (__predict_false(UINTPTR_MAX == min_vaddr || base < min_vaddr)) return 0;
  uintptr_t load_bias = base - min_vaddr;

  return load_bias;
}

static void sh_exit_init_in_library(void) {
  sh_exit_exec_load_bias = sh_exit_get_load_bias_from_aux(AT_PHDR);
  sh_exit_linker_load_bias = sh_exit_get_load_bias_from_aux(AT_BASE);
  sh_exit_vdso_load_bias = sh_exit_get_load_bias_from_aux(AT_SYSINFO_EHDR);
}

static bool sh_exit_is_elf_loaded_by_kernel(uintptr_t load_bias) {
  if (0 != sh_exit_exec_load_bias && sh_exit_exec_load_bias == load_bias) return true;
  if (0 != sh_exit_linker_load_bias && sh_exit_linker_load_bias == load_bias) return true;
  if (0 != sh_exit_vdso_load_bias && sh_exit_vdso_load_bias == load_bias) return true;
  return false;
}

static void sh_exit_destroy_elf_info(sh_exit_elf_info_t *elfinfo) {
  for (size_t i = 0; i < elfinfo->gaps_num; i++) {
    sh_exit_elf_gap_t *gap = &elfinfo->gaps[i];
    if (NULL != gap->flags) free(gap->flags);
  }
  if (NULL != elfinfo->gaps) free(elfinfo->gaps);
  free(elfinfo);
}

static sh_exit_elf_info_t *sh_exit_create_elf_info(xdl_info_t *dlinfo) {
  sh_exit_elf_info_t *elfinfo = calloc(1, sizeof(sh_exit_elf_info_t));
  if (NULL == elfinfo) return NULL;
  elfinfo->dli_fbase = dlinfo->dli_fbase;
  elfinfo->dlpi_phdr = dlinfo->dlpi_phdr;
  elfinfo->dlpi_phnum = dlinfo->dlpi_phnum;
  elfinfo->gaps = NULL;
  elfinfo->gaps_num = 0;

  size_t gaps_max = 0;
  for (size_t i = 0; i < dlinfo->dlpi_phnum; i++)
    if (PT_LOAD == dlinfo->dlpi_phdr[i].p_type) gaps_max++;
  if (gaps_max > 0) {
    elfinfo->gaps = calloc(1, sizeof(sh_exit_elf_gap_t) * gaps_max);
    if (NULL == elfinfo->gaps) goto err;
  }

  bool elf_loaded_by_kernel = sh_exit_is_elf_loaded_by_kernel((uintptr_t)dlinfo->dli_fbase);

  for (size_t i = 0; i < dlinfo->dlpi_phnum; i++) {
    // current LOAD segment
    const ElfW(Phdr) *cur_phdr = &(dlinfo->dlpi_phdr[i]);
    if (PT_LOAD != cur_phdr->p_type) continue;

    // next LOAD segment
    const ElfW(Phdr) *next_phdr = NULL;
    if (!elf_loaded_by_kernel) {
      for (size_t j = i + 1; j < dlinfo->dlpi_phnum; j++) {
        if (PT_LOAD == dlinfo->dlpi_phdr[j].p_type) {
          next_phdr = &(dlinfo->dlpi_phdr[j]);
          break;
        }
      }
    }

    uintptr_t cur_end = (uintptr_t)dlinfo->dli_fbase + cur_phdr->p_vaddr + cur_phdr->p_memsz;
    uintptr_t cur_page_end = sh_util_page_end(cur_end);
    uintptr_t cur_file_end = (uintptr_t)dlinfo->dli_fbase + cur_phdr->p_vaddr + cur_phdr->p_filesz;
    uintptr_t cur_file_page_end = sh_util_page_end(cur_file_end);
    uintptr_t next_page_start =
        (NULL == next_phdr ? cur_page_end
                           : sh_util_page_start((uintptr_t)dlinfo->dli_fbase + next_phdr->p_vaddr));

    uintptr_t gap_start = 0, gap_end = 0;
    if (cur_phdr->p_flags & PF_X) {
      // From: last PF_X page's unused memory tail space.
      // To: next page start.
      gap_start = SH_UTIL_ALIGN_END(cur_end, SH_EXIT_SZ);
      gap_end = next_page_start;
    } else if (cur_page_end > cur_file_page_end) {
      // From: last .bss page(which must NOT be file backend)'s unused memory tail space.
      // To: next page start.
      gap_start = SH_UTIL_ALIGN_END(cur_end, SH_EXIT_SZ);
      gap_end = next_page_start;
    } else if (next_page_start > cur_page_end) {
      // Entire unused memory pages.
      gap_start = cur_page_end;
      gap_end = next_page_start;
    }

    if (gap_start < gap_end) {
      SH_LOG_INFO("exit: gap, %" PRIxPTR " - %" PRIxPTR " (load_bias %" PRIxPTR ", %" PRIxPTR " - %" PRIxPTR
                  ")",
                  gap_start, gap_end, (uintptr_t)dlinfo->dli_fbase, gap_start - (uintptr_t)dlinfo->dli_fbase,
                  gap_end - (uintptr_t)dlinfo->dli_fbase);

      sh_exit_elf_gap_t *gap = &elfinfo->gaps[elfinfo->gaps_num];
      elfinfo->gaps_num++;
      gap->start = gap_start;
      gap->end = gap_end;
      size_t item_num = (gap->end - gap->start) / SH_EXIT_SZ;
      gap->flags = calloc(1, sizeof(uint32_t) * item_num);
      if (NULL == gap->flags) goto err;
    }
  }

  return elfinfo;

err:
  sh_exit_destroy_elf_info(elfinfo);
  return NULL;
}

static sh_exit_elf_info_t *sh_exit_find_elf_info_by_pc(uintptr_t pc) {
  sh_exit_elf_info_t *elfinfo;
  TAILQ_FOREACH(elfinfo, &sh_exit_elf_infos, link) {
    if (sh_util_is_in_elf_pt_load(elfinfo->dli_fbase, elfinfo->dlpi_phdr, elfinfo->dlpi_phnum, pc))
      return elfinfo;
  }
  return NULL;
}

static int sh_exit_alloc_in_elf_gap(uintptr_t *exit_addr, uintptr_t pc, sh_exit_elf_info_t *elfinfo,
                                    sh_exit_elf_gap_t *gap, uint8_t *exit, size_t range_low,
                                    size_t range_high, uint32_t now) {
  // fix the start of the range according to the "range_low"
  uintptr_t start = gap->start;
  if (pc >= range_low) start = SH_UTIL_MAX(start, pc - range_low);
  start = SH_UTIL_ALIGN_END(start, SH_EXIT_SZ);

  // fix the end of the range according to the "range_high"
  uintptr_t end = gap->end;
  if (range_high <= UINTPTR_MAX - pc - SH_EXIT_SZ) end = SH_UTIL_MIN(end, pc + range_high + SH_EXIT_SZ);
  end = SH_UTIL_ALIGN_START(end, SH_EXIT_SZ);

  if (start >= end) return -1;

  // calculate index range: [start_idx, end_idx)
  size_t start_idx = (start - gap->start) / SH_EXIT_SZ;
  size_t end_idx = (end - gap->start) / SH_EXIT_SZ;

  SH_LOG_INFO("exit: fixed gap, %" PRIxPTR " - %" PRIxPTR " (load_bias %" PRIxPTR ", %" PRIxPTR " - %" PRIxPTR
              "), idx %zu - %zu",
              start, end, (uintptr_t)elfinfo->dli_fbase, start - (uintptr_t)elfinfo->dli_fbase,
              end - (uintptr_t)elfinfo->dli_fbase, start_idx, end_idx);

  for (size_t i = start_idx; i < end_idx; i++) {
    // check if used
    uint32_t used = gap->flags[i] >> 31;
    if (used) continue;

    // check timestamp
    uint32_t ts = gap->flags[i] & 0x7FFFFFFF;
    if (now <= ts || now - ts <= SH_EXIT_DELAY_SEC) continue;

    // write shellcode to the current location
    uintptr_t cur = gap->start + i * SH_EXIT_SZ;
    if (0 != sh_util_mprotect(cur, SH_EXIT_SZ, PROT_READ | PROT_WRITE | PROT_EXEC)) return -1;
    memcpy((void *)cur, exit, SH_EXIT_SZ);
    sh_util_clear_cache(cur, SH_EXIT_SZ);
    *exit_addr = cur;  // OK

    // mark the current item as used
    gap->flags[i] |= 0x80000000;

    SH_LOG_INFO("exit: in-library alloc, at %" PRIxPTR " (load_bias %" PRIxPTR ", %" PRIxPTR "), len %zu",
                cur, (uintptr_t)elfinfo->dli_fbase, cur - (uintptr_t)elfinfo->dli_fbase, (size_t)SH_EXIT_SZ);
    return 0;
  }

  return -1;
}

static int sh_exit_alloc_in_library(uintptr_t *exit_addr, uintptr_t pc, xdl_info_t *dlinfo, uint8_t *exit,
                                    size_t range_low, size_t range_high) {
  int r = -1;
  *exit_addr = 0;

  pthread_mutex_lock(&sh_exit_elf_infos_lock);

  // find or create elfinfo
  sh_exit_elf_info_t *elfinfo = sh_exit_find_elf_info_by_pc(pc);
  if (NULL == elfinfo) {
    // get dlinfo by pc
    if (NULL == dlinfo->dli_fbase || NULL == dlinfo->dlpi_phdr) {
      xdl_info_t dlinfo_tmp;
      dlinfo = &dlinfo_tmp;
      if (0 != (r = sh_linker_get_dlinfo_by_addr((void *)pc, dlinfo, true))) goto end;
    }

    // create elfinfo by dlinfo
    if (NULL == (elfinfo = sh_exit_create_elf_info(dlinfo))) {
      r = SHADOWHOOK_ERRNO_OOM;
      goto end;
    }

    // save new elfinfo
    TAILQ_INSERT_TAIL(&sh_exit_elf_infos, elfinfo, link);
  }

  uint32_t now = (uint32_t)sh_util_get_stable_timestamp();
  SH_SIG_TRY(SIGSEGV, SIGBUS) {
    // try to alloc space in each gaps of current ELF
    for (size_t i = 0; i < elfinfo->gaps_num; i++) {
      if (0 == (r = sh_exit_alloc_in_elf_gap(exit_addr, pc, elfinfo, &elfinfo->gaps[i], exit, range_low,
                                             range_high, now)))
        break;  // OK
    }
  }
  SH_SIG_CATCH() {
    r = -1;
    *exit_addr = 0;
    SH_LOG_WARN("exit: alloc crashed");
  }
  SH_SIG_EXIT

end:
  pthread_mutex_unlock(&sh_exit_elf_infos_lock);
  return r;
}

static int sh_exit_free_in_library(uintptr_t exit_addr, uint8_t *exit) {
  // check if the content matches
  int r = 0;
  SH_SIG_TRY(SIGSEGV, SIGBUS) {
    if (0 != memcmp((void *)exit_addr, exit, SH_EXIT_SZ)) {
      r = SHADOWHOOK_ERRNO_UNHOOK_EXIT_MISMATCH;
    }
    r = 0;
  }
  SH_SIG_CATCH() {
    r = SHADOWHOOK_ERRNO_UNHOOK_EXIT_CRASH;
    SH_LOG_WARN("exit: free crashed");
  }
  SH_SIG_EXIT
  if (0 != r) return r;

  pthread_mutex_lock(&sh_exit_elf_infos_lock);
  sh_exit_elf_info_t *elfinfo;
  TAILQ_FOREACH(elfinfo, &sh_exit_elf_infos, link) {
    for (size_t i = 0; i < elfinfo->gaps_num; i++) {
      sh_exit_elf_gap_t *gap = &elfinfo->gaps[i];
      if (gap->start <= exit_addr && exit_addr < gap->end) {
        size_t j = (exit_addr - gap->start) / SH_EXIT_SZ;
        uint32_t now = (uint32_t)sh_util_get_stable_timestamp();
        gap->flags[j] = now & 0x7FFFFFFF;
        goto end;
      }
    }
  }
end:
  pthread_mutex_unlock(&sh_exit_elf_infos_lock);
  return 0;
}

//
// public APIs
//
void sh_exit_init(void) {
  sh_exit_init_out_library();
  sh_exit_init_in_library();
}

int sh_exit_alloc(uintptr_t *exit_addr, uint16_t *exit_type, uintptr_t pc, xdl_info_t *dlinfo, uint8_t *exit,
                  size_t range_low, size_t range_high) {
  int r;

  // (1) try out-library mode first. Because ELF gaps are a valuable non-renewable resource.
  *exit_type = SH_EXIT_TYPE_OUT_LIBRARY;
  r = sh_exit_alloc_out_library(exit_addr, pc, exit, range_low, range_high);
  if (0 == r) goto ok;

  // (2) try in-library mode.
  *exit_type = SH_EXIT_TYPE_IN_LIBRARY;
  r = sh_exit_alloc_in_library(exit_addr, pc, dlinfo, exit, range_low, range_high);
  if (0 == r) goto ok;

  return r;

ok:
  SH_LOG_INFO("exit: alloc %s library, exit %" PRIxPTR ", pc %" PRIxPTR ", distance %" PRIxPTR
              ", range [-%zx, %zx]",
              (*exit_type == SH_EXIT_TYPE_OUT_LIBRARY ? "out" : "in"), *exit_addr, pc,
              (pc > *exit_addr ? pc - *exit_addr : *exit_addr - pc), range_low, range_high);
  return 0;
}

int sh_exit_free(uintptr_t exit_addr, uint16_t exit_type, uint8_t *exit) {
  if (SH_EXIT_TYPE_OUT_LIBRARY == exit_type) {
    (void)exit;
    sh_exit_free_out_library(exit_addr);
    return 0;
  } else
    return sh_exit_free_in_library(exit_addr, exit);
}

void sh_exit_free_after_dlclose(uintptr_t exit_addr, uint16_t exit_type) {
  if (SH_EXIT_TYPE_OUT_LIBRARY == exit_type) {
    sh_exit_free_out_library(exit_addr);
  }
}

void sh_exit_free_after_dlclose_by_dlinfo(xdl_info_t *dlinfo) {
  sh_exit_elf_info_t *elfinfo, *elfinfo_tmp;

  pthread_mutex_lock(&sh_exit_elf_infos_lock);
  TAILQ_FOREACH_SAFE(elfinfo, &sh_exit_elf_infos, link, elfinfo_tmp) {
    if (elfinfo->dli_fbase == dlinfo->dli_fbase) {
      TAILQ_REMOVE(&sh_exit_elf_infos, elfinfo, link);
      break;
    }
  }
  pthread_mutex_unlock(&sh_exit_elf_infos_lock);

  if (NULL != elfinfo) sh_exit_destroy_elf_info(elfinfo);
}

#pragma clang diagnostic pop
