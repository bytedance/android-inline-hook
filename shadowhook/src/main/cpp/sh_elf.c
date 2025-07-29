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

// Created by Kelun Cai (caikelun@bytedance.com) on 2024-12-30.

#include "sh_elf.h"

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

#define SH_ELF_DELAY_SEC 3

#if defined(__arm__)
#define SH_ELF_UNIT_SIZE 8
#elif defined(__aarch64__)
#define SH_ELF_UNIT_SIZE 4
#endif

extern __attribute((weak)) unsigned long int getauxval(unsigned long int);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
  const char *sym_name;
  uint8_t min_api_level;
  uint8_t max_api_level;
  bool try_dynsym;
  bool try_symtab;
} sh_elf_useless_symbol_t;
#pragma clang diagnostic pop

typedef struct {
  const char *lib_name;
  sh_elf_useless_symbol_t *symbols;
  size_t symbols_num;
} sh_elf_useless_t;

#define TO_STR_HELPER(x)         #x
#define TO_STR(x)                TO_STR_HELPER(x)
#define USELESS_ITEM(lib, array) {TO_STR(lib), array, sizeof(array) / sizeof(array[0])}

// The idea and design of "using useless symbol's memory space as island" was originally done
// by Lei Tong(tonglei.7274@bytedance.com) in ArtShadowHook. ShadowHook merged this design.

// clang-format off
// linker
static sh_elf_useless_symbol_t sh_elf_useless_linker[] = {
    // __linker_init
    {"__dl___linker_init", 21, 36, false, true},
// __linker_init_post_relocation
#if defined(__arm__)
    {"__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlockj", 21, 26, false, true},
#elif defined(__aarch64__)
    {"__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlocky", 21, 26, false, true},
#endif
    {"__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlock", 27, 28, false, true},
    {"__dl__ZL29__linker_init_post_relocationR19KernelArgumentBlockR6soinfo", 29, 36, false, true}
};
// libart.so
static sh_elf_useless_symbol_t sh_elf_useless_libart[] = {
    // art::Runtime::Start
    {"_ZN3art7Runtime5StartEv", 21, 36, true, true},
    // art::Runtime::Init
    {"_ZN3art7Runtime4InitERKNSt3__16vectorINS1_4pairINS1_12basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEEPKvEENS7_ISC_EEEEb",
     21, 23, true, true},
    {"_ZN3art7Runtime4InitEONS_18RuntimeArgumentMapE", 24, 36, true, true}
};
// libandroid_runtime.so
static sh_elf_useless_symbol_t sh_elf_useless_libandroid_runtime[] = {
    // android::AndroidRuntime::start
    {"_ZN7android14AndroidRuntime5startEPKcRKNS_6VectorINS_7String8EEE", 21, 22, true, true},
    {"_ZN7android14AndroidRuntime5startEPKcRKNS_6VectorINS_7String8EEEb", 23, 36, true, true},
    // android::AndroidRuntime::startVm
    {"_ZN7android14AndroidRuntime7startVmEPP7_JavaVMPP7_JNIEnv", 21, 22, true, true},
    {"_ZN7android14AndroidRuntime7startVmEPP7_JavaVMPP7_JNIEnvb", 23, 29, true, true},
    {"_ZN7android14AndroidRuntime7startVmEPP7_JavaVMPP7_JNIEnvbb", 30, 36, true, true}
};
static sh_elf_useless_t sh_elf_useless[] = {
#if defined(__arm__)
    USELESS_ITEM(linker, sh_elf_useless_linker),
#elif defined(__aarch64__)
    USELESS_ITEM(linker64, sh_elf_useless_linker),
#endif
    USELESS_ITEM(libart.so, sh_elf_useless_libart),
    USELESS_ITEM(libandroid_runtime.so, sh_elf_useless_libandroid_runtime)
};
// clang-format on

// ELF gap, range: [start, end)
typedef struct {
  uintptr_t start;
  uintptr_t end;
  size_t trampo_count;
  uint32_t *flags;  // flags for each unit: 1 bit for used/unused, 31 bits for timestamp
} sh_elf_gap_t;

// ELF info
typedef struct sh_elf {
  void *dli_fbase;
  const ElfW(Phdr) *dlpi_phdr;
  size_t dlpi_phnum;
  sh_elf_gap_t *gaps;
  size_t gaps_num;
  sh_elf_useless_t *useless;
  TAILQ_ENTRY(sh_elf, ) link;
} sh_elf_t;
typedef TAILQ_HEAD(sh_elf_queue, sh_elf, ) sh_elf_queue_t;

// ELF info queue
static sh_elf_queue_t sh_elfs = TAILQ_HEAD_INITIALIZER(sh_elfs);
static pthread_mutex_t sh_elfs_lock = PTHREAD_MUTEX_INITIALIZER;

static uintptr_t sh_elf_get_load_bias_from_aux(unsigned long type) {
  if (__predict_false(NULL == getauxval)) return 0;

  uintptr_t val = (uintptr_t)getauxval(type);
  if (__predict_false(0 == val)) return 0;

  // get base
  uintptr_t base = (AT_PHDR == type ? sh_util_page_start(val) : val);
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

static bool sh_elf_is_loaded_by_kernel(uintptr_t load_bias) {
  static uintptr_t exec_load_bias = UINTPTR_MAX;
  static uintptr_t linker_load_bias = UINTPTR_MAX;
  static uintptr_t vdso_load_bias = UINTPTR_MAX;

  if (__predict_false(UINTPTR_MAX == exec_load_bias)) {
    exec_load_bias = sh_elf_get_load_bias_from_aux(AT_PHDR);
  }
  if (__predict_false(UINTPTR_MAX == linker_load_bias)) {
    linker_load_bias = sh_elf_get_load_bias_from_aux(AT_BASE);
  }
  if (__predict_false(UINTPTR_MAX == vdso_load_bias)) {
    vdso_load_bias = sh_elf_get_load_bias_from_aux(AT_SYSINFO_EHDR);
  }

  if (0 != exec_load_bias && exec_load_bias == load_bias) return true;
  if (0 != linker_load_bias && linker_load_bias == load_bias) return true;
  if (0 != vdso_load_bias && vdso_load_bias == load_bias) return true;
  return false;
}

static void sh_elf_destroy(sh_elf_t *elf) {
  for (size_t i = 0; i < elf->gaps_num; i++) {
    sh_elf_gap_t *gap = &elf->gaps[i];
    if (NULL != gap->flags) free(gap->flags);
  }
  if (NULL != elf->gaps) free(elf->gaps);
  free(elf);
}

static int sh_elf_get_gaps_from_phdr(sh_elf_t *elf, sh_addr_info_t *addr_info) {
  bool elf_loaded_by_kernel = sh_elf_is_loaded_by_kernel((uintptr_t)addr_info->dli_fbase);

  for (size_t i = 0; i < addr_info->dlpi_phnum; i++) {
    // current LOAD segment
    const ElfW(Phdr) *cur_phdr = &(addr_info->dlpi_phdr[i]);
    if (PT_LOAD != cur_phdr->p_type) continue;

    // next LOAD segment
    const ElfW(Phdr) *next_phdr = NULL;
    if (!elf_loaded_by_kernel) {
      for (size_t j = i + 1; j < addr_info->dlpi_phnum; j++) {
        if (PT_LOAD == addr_info->dlpi_phdr[j].p_type) {
          next_phdr = &(addr_info->dlpi_phdr[j]);
          break;
        }
      }
    }

    uintptr_t cur_end = (uintptr_t)addr_info->dli_fbase + cur_phdr->p_vaddr + cur_phdr->p_memsz;
    uintptr_t cur_page_end = sh_util_page_end(cur_end);
    uintptr_t cur_file_end = (uintptr_t)addr_info->dli_fbase + cur_phdr->p_vaddr + cur_phdr->p_filesz;
    uintptr_t cur_file_page_end = sh_util_page_end(cur_file_end);
    uintptr_t next_page_start =
        (NULL == next_phdr ? cur_page_end
                           : sh_util_page_start((uintptr_t)addr_info->dli_fbase + next_phdr->p_vaddr));

    uintptr_t gap_start = 0, gap_end = 0;
    if (cur_phdr->p_flags & PF_X) {
      // From: last PF_X page's unused memory tail space.
      // To: next page start.
      gap_start = SH_UTIL_ALIGN_END(cur_end, SH_ELF_UNIT_SIZE);
      gap_end = next_page_start;
    } else if (cur_page_end > cur_file_page_end) {
      // From: last .bss page(which must NOT be file backend)'s unused memory tail space.
      // To: next page start.
      gap_start = SH_UTIL_ALIGN_END(cur_end, SH_ELF_UNIT_SIZE);
      gap_end = next_page_start;
    } else if (next_page_start > cur_page_end) {
      // Entire unused memory pages.
      gap_start = cur_page_end;
      gap_end = next_page_start;
    }
    if (gap_end - gap_start < SH_ELF_UNIT_SIZE) continue;

    SH_LOG_INFO("elf: find phdr gap %" PRIxPTR "-%" PRIxPTR "(load_bias %" PRIxPTR ", %" PRIxPTR "-%" PRIxPTR
                ")",
                gap_start, gap_end, (uintptr_t)addr_info->dli_fbase,
                gap_start - (uintptr_t)addr_info->dli_fbase, gap_end - (uintptr_t)addr_info->dli_fbase);

    sh_elf_gap_t *gap = &elf->gaps[elf->gaps_num];
    elf->gaps_num++;
    gap->start = gap_start;
    gap->end = gap_end;
    gap->trampo_count = (gap->end - gap->start) / SH_ELF_UNIT_SIZE;
    gap->flags = calloc(1, sizeof(uint32_t) * gap->trampo_count);
    if (NULL == gap->flags) return -1;
  }

  return 0;
}

static void sh_elf_get_gaps_from_useless_symbols(sh_elf_t *elf) {
  void *handle = xdl_open(elf->useless->lib_name, XDL_DEFAULT);
  if (NULL == handle) return;

  uint8_t api_level = (uint8_t)sh_util_get_api_level();

  for (size_t i = 0; i < elf->useless->symbols_num; i++) {
    sh_elf_useless_symbol_t *sym = &elf->useless->symbols[i];

    if (sym->min_api_level <= api_level && api_level <= sym->max_api_level) {
      void *sym_addr = NULL;
      size_t sym_sz = 0;
      if (sym->try_dynsym) {
        sym_addr = xdl_sym(handle, sym->sym_name, &sym_sz);
      }
      if ((NULL == sym_addr || 0 == sym_sz) && sym->try_symtab) {
        sym_addr = xdl_dsym(handle, sym->sym_name, &sym_sz);
      }
      if (NULL == sym_addr || 0 == sym_sz) continue;

      uintptr_t gap_start = SH_UTIL_ALIGN_END(sym_addr, SH_ELF_UNIT_SIZE);
      uintptr_t gap_end = SH_UTIL_ALIGN_START((uintptr_t)sym_addr + sym_sz, SH_ELF_UNIT_SIZE);
      if (gap_end - gap_start < SH_ELF_UNIT_SIZE) continue;
      SH_LOG_INFO("elf: lazy find sym gap %" PRIxPTR "-%" PRIxPTR, gap_start, gap_end);

      sh_elf_gap_t *gap = &elf->gaps[elf->gaps_num];
      elf->gaps_num++;
      gap->start = gap_start;
      gap->end = gap_end;
      gap->trampo_count = (gap->end - gap->start) / SH_ELF_UNIT_SIZE;
      gap->flags = calloc(1, sizeof(uint32_t) * gap->trampo_count);
      if (NULL == gap->flags) goto end;
    }
  }

end:
  if (NULL != handle) xdl_close(handle);
}

static sh_elf_t *sh_elf_create(sh_addr_info_t *addr_info, char *fname) {
  sh_elf_t *elf = malloc(sizeof(sh_elf_t));
  if (NULL == elf) return NULL;
  elf->dli_fbase = addr_info->dli_fbase;
  elf->dlpi_phdr = addr_info->dlpi_phdr;
  elf->dlpi_phnum = addr_info->dlpi_phnum;
  elf->gaps = NULL;
  elf->gaps_num = 0;
  elf->useless = NULL;

  size_t gaps_max = 0;
  for (size_t i = 0; i < addr_info->dlpi_phnum; i++) {
    if (PT_LOAD == addr_info->dlpi_phdr[i].p_type) gaps_max++;
  }
  if (NULL != fname) {
    for (size_t i = 0; i < sizeof(sh_elf_useless) / sizeof(sh_elf_useless[0]); i++) {
      sh_elf_useless_t *lib = &sh_elf_useless[i];
      if (sh_util_match_pathname(fname, lib->lib_name)) {
        uint8_t api_level = (uint8_t)sh_util_get_api_level();
        for (size_t j = 0; j < lib->symbols_num; j++) {
          sh_elf_useless_symbol_t *sym = &lib->symbols[j];
          if (sym->min_api_level <= api_level && api_level <= sym->max_api_level) {
            elf->useless = lib;
            gaps_max++;
          }
        }
        break;
      }
    }
  }

  if (gaps_max > 0) {
    elf->gaps = calloc(1, sizeof(sh_elf_gap_t) * gaps_max);
    if (NULL == elf->gaps) goto err;

    if (0 != sh_elf_get_gaps_from_phdr(elf, addr_info)) goto err;
  }

  return elf;

err:
  sh_elf_destroy(elf);
  return NULL;
}

static sh_elf_t *sh_elf_find_by_pc(uintptr_t pc) {
  sh_elf_t *elf;
  TAILQ_FOREACH(elf, &sh_elfs, link) {
    if (sh_linker_is_addr_in_elf_pt_load(pc, elf->dli_fbase, elf->dlpi_phdr, elf->dlpi_phnum)) return elf;
  }
  return NULL;
}

// range: [range_low, range_high]
static uintptr_t sh_elf_alloc_in_gap(size_t size, uintptr_t range_low, uintptr_t range_high, sh_elf_t *elf,
                                     sh_elf_gap_t *gap, uint32_t now) {
  // arm   : size = 8             , SH_ELF_UNIT_SIZE = 8
  // arm64 : size = 8 or 16 or 20 , SH_ELF_UNIT_SIZE = 4
  size_t n_unit = size / SH_ELF_UNIT_SIZE;  // the remainder is definitely 0

  for (size_t i = 0; i < gap->trampo_count - n_unit + 1; i++) {
    // check current trampo's range
    uintptr_t addr = gap->start + SH_ELF_UNIT_SIZE * i;
    if ((addr < range_low) || (range_high < addr)) continue;

    size_t j;
    for (j = i; j < i + n_unit; j++) {
      // check if used
      uint32_t used = gap->flags[j] >> 31;
      if (used) break;

      // check timestamp
      uint32_t ts = gap->flags[j] & 0x7FFFFFFF;
      if (now <= ts || now - ts <= SH_ELF_DELAY_SEC) break;
    }
    if (j < i + n_unit) {
      i = j;  // skip unavailable unit(s)
      continue;
    }

    // make sure the current trampo is available
    if (0 != sh_util_mprotect(addr, size, PROT_READ | PROT_WRITE | PROT_EXEC)) return 0;

    // mark the current unit(s) as used
    for (j = i; j < i + n_unit; j++) gap->flags[j] |= 0x80000000;

    SH_LOG_INFO("elf: alloc addr %" PRIxPTR "(load_bias %" PRIxPTR ", %" PRIxPTR
                "), size %zu, idx [%zu, %zu)",
                addr, (uintptr_t)elf->dli_fbase, addr - (uintptr_t)elf->dli_fbase, size, i, i + n_unit);
    return addr;
  }

  return 0;
}

uintptr_t sh_elf_alloc(size_t size, uintptr_t range_low, uintptr_t range_high, uintptr_t pc,
                       sh_addr_info_t *addr_info) {
  size = SH_UTIL_ALIGN_END(size, SH_ELF_UNIT_SIZE);
  uintptr_t addr = 0;

  pthread_mutex_lock(&sh_elfs_lock);

  // find or create sh_elf_t object
  sh_elf_t *elf = sh_elf_find_by_pc(pc);
  if (NULL == elf) {
    // get address info by pc
    char fname[1024] = {'\0'};
    if (NULL == addr_info->dli_fbase) {
      if (0 != sh_linker_get_addr_info_by_addr((void *)pc, addr_info->is_sym_addr, addr_info->is_proc_start,
                                               addr_info, true, fname, sizeof(fname)))
        goto end;
    } else {
      sh_linker_get_fname_by_fbase(addr_info->dli_fbase, fname, sizeof(fname));
    }

    // create sh_elf_t object by dlinfo
    if (NULL == (elf = sh_elf_create(addr_info, '\0' == fname[0] ? NULL : fname))) goto end;

    // save new sh_elf_t object
    TAILQ_INSERT_TAIL(&sh_elfs, elf, link);
  }

  // try to alloc space in each gaps of current ELF
  uint32_t now = (uint32_t)sh_util_get_stable_timestamp();
  for (size_t i = 0; i < elf->gaps_num; i++) {
    if (0 != (addr = sh_elf_alloc_in_gap(size, range_low, range_high, elf, &elf->gaps[i], now)))
      goto end;  // OK
  }

  // lazy load all useless symbols gaps(only once), and try to alloc space again
  if (NULL != elf->useless) {
    size_t gaps_num_old = elf->gaps_num;
    sh_elf_get_gaps_from_useless_symbols(elf);
    elf->useless = NULL;
    if (elf->gaps_num > gaps_num_old) {
      now = (uint32_t)sh_util_get_stable_timestamp();
      for (size_t i = gaps_num_old; i < elf->gaps_num; i++) {
        if (0 != (addr = sh_elf_alloc_in_gap(size, range_low, range_high, elf, &elf->gaps[i], now)))
          goto end;  // OK
      }
    }
  }

end:
  pthread_mutex_unlock(&sh_elfs_lock);
  return addr;
}

void sh_elf_free(uintptr_t addr, size_t size) {
  uint32_t now = (uint32_t)sh_util_get_stable_timestamp();
  size = SH_UTIL_ALIGN_END(size, SH_ELF_UNIT_SIZE);
  size_t n_unit = size / SH_ELF_UNIT_SIZE;

  pthread_mutex_lock(&sh_elfs_lock);
  sh_elf_t *elf;
  TAILQ_FOREACH(elf, &sh_elfs, link) {
    for (size_t i = 0; i < elf->gaps_num; i++) {
      sh_elf_gap_t *gap = &elf->gaps[i];
      if (gap->start <= addr && addr < gap->end) {
        size_t j = (addr - gap->start) / SH_ELF_UNIT_SIZE;
        for (size_t k = j; k < j + n_unit; k++) {
          gap->flags[k] = now & 0x7FFFFFFF;
        }
        SH_LOG_INFO("elf: free addr %" PRIxPTR "(load_bias %" PRIxPTR ", %" PRIxPTR
                    "), size %zu, gap %zu, idx [%zu, %zu)",
                    addr, (uintptr_t)elf->dli_fbase, addr - (uintptr_t)elf->dli_fbase, size, i, j,
                    j + n_unit);
        goto end;
      }
    }
  }
end:
  pthread_mutex_unlock(&sh_elfs_lock);
}

void sh_elf_cleanup_after_dlclose(uintptr_t load_bias) {
  sh_elf_t *elf, *elf_tmp;

  pthread_mutex_lock(&sh_elfs_lock);
  TAILQ_FOREACH_SAFE(elf, &sh_elfs, link, elf_tmp) {
    if (elf->dli_fbase == (void *)load_bias) {
      TAILQ_REMOVE(&sh_elfs, elf, link);
      break;
    }
  }
  pthread_mutex_unlock(&sh_elfs_lock);

  if (NULL != elf) sh_elf_destroy(elf);
}
