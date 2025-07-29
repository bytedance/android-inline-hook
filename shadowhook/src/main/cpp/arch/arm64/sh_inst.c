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

#include "sh_inst.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "sh_a64.h"
#include "sh_config.h"
#include "sh_enter.h"
#include "sh_island.h"
#include "sh_linker.h"
#include "sh_log.h"
#include "sh_sig.h"
#include "sh_util.h"
#include "shadowhook.h"

static int sh_inst_rewrite(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                           uintptr_t resume_addr, sh_inst_set_orig_addr_t set_orig_addr,
                           void *set_orig_addr_arg) {
  // backup original instructions (length: 4 or 16 or 24)
  memcpy((void *)(self->backup), (void *)target_addr, self->backup_len);

  // package the information passed to rewrite
  sh_a64_rewrite_info_t rinfo;
  rinfo.start_addr = target_addr;
  rinfo.end_addr = target_addr + self->backup_len;
  rinfo.buf = (uint32_t *)self->enter;
  rinfo.buf_offset = 0;
  rinfo.inst_prolog_len = 0;
  rinfo.inst_lens_cnt = self->backup_len / 4;
  for (uintptr_t i = 0; i < self->backup_len; i += 4)
    rinfo.inst_lens[i / 4] = sh_a64_get_rewrite_inst_len(*((uint32_t *)(target_addr + i)));
  rinfo.island_rewrite = (4 == self->backup_len && !addr_info->is_proc_start) ? &self->island_rewrite : NULL;

  if (!addr_info->is_proc_start) {
    rinfo.buf_offset += sh_a64_restore_ip((uint32_t *)self->enter);
    rinfo.inst_prolog_len = rinfo.buf_offset;
  }

  // rewrite original instructions (fill in enter)
  uintptr_t pc = target_addr;
  for (uintptr_t i = 0; i < self->backup_len; i += 4, pc += 4) {
    size_t offset = sh_a64_rewrite((uint32_t *)(self->enter + rinfo.buf_offset),
                                   *((uint32_t *)(target_addr + i)), pc, &rinfo);
    if (0 == offset) return SHADOWHOOK_ERRNO_HOOK_REWRITE_FAILED;
    rinfo.buf_offset += offset;
  }

  // absolute jump back to remaining original instructions (fill in enter)
  if (addr_info->is_proc_start)
    rinfo.buf_offset +=
        sh_a64_absolute_jump_with_ret_ip((uint32_t *)(self->enter + rinfo.buf_offset), resume_addr);
  else
    rinfo.buf_offset +=
        sh_a64_absolute_jump_with_ret_rx((uint32_t *)(self->enter + rinfo.buf_offset), resume_addr);
  sh_util_clear_cache(self->enter, rinfo.buf_offset);

  // save original function address
  if (NULL != set_orig_addr) set_orig_addr(self->enter, set_orig_addr_arg);
  return 0;
}

static int sh_inst_safe_rewrite(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                uintptr_t resume_addr, sh_inst_set_orig_addr_t set_orig_addr,
                                void *set_orig_addr_arg) {
  if (0 != sh_util_mprotect(target_addr, self->backup_len, PROT_READ | PROT_WRITE | PROT_EXEC))
    return SHADOWHOOK_ERRNO_MPROT;

  int r;
  SH_SIG_TRY(SIGSEGV, SIGBUS) {
    r = sh_inst_rewrite(self, target_addr, addr_info, resume_addr, set_orig_addr, set_orig_addr_arg);
  }
  SH_SIG_CATCH() {
    return SHADOWHOOK_ERRNO_HOOK_REWRITE_CRASH;
  }
  SH_SIG_EXIT
  return r;
}

#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND

// B: [-128M, +128M - 4]
#define SH_INST_A64_B_OFFSET_LOW  (134217728)
#define SH_INST_A64_B_OFFSET_HIGH (134217724)

static int sh_inst_rewrite_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                       sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  uintptr_t pc = target_addr;
  self->backup_len = 4;
  uintptr_t resume_addr;

  if (addr_info->is_proc_start) {
    resume_addr = target_addr + self->backup_len;
  } else {
    // alloc an island-enter (jump from "island-enter.addr + 4" to "pc + 4")
    uintptr_t island_enter_range_low = pc > SH_INST_A64_B_OFFSET_HIGH ? pc - SH_INST_A64_B_OFFSET_HIGH : 0;
    uintptr_t island_enter_range_high =
        UINTPTR_MAX - pc > SH_INST_A64_B_OFFSET_LOW ? pc + SH_INST_A64_B_OFFSET_LOW : UINTPTR_MAX;
    sh_island_alloc(&self->island_enter, 8, island_enter_range_low, island_enter_range_high, pc, addr_info);
    if (0 == self->island_enter.addr) return SHADOWHOOK_ERRNO_HOOK_ISLAND_ENTER;

    // relative jump to "pc + 4" in island-enter
    sh_a64_restore_rx((uint32_t *)self->island_enter.addr);
    sh_a64_relative_jump((uint32_t *)(self->island_enter.addr + 4), target_addr + self->backup_len,
                         self->island_enter.addr + 4);
    sh_util_clear_cache(self->island_enter.addr, self->island_enter.size);

    resume_addr = self->island_enter.addr;
  }

  int r;
  if (0 != (r = sh_inst_safe_rewrite(self, target_addr, addr_info, resume_addr, set_orig_addr,
                                     set_orig_addr_arg))) {
    if (0 != self->island_enter.addr) sh_island_free(&self->island_enter);
    if (0 != self->island_rewrite.addr) sh_island_free(&self->island_rewrite);
  }
  return r;
}

static int sh_inst_reloc_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                     uintptr_t new_addr, bool is_to_interceptor, bool is_rehook) {
  int r;
  uintptr_t pc = target_addr;
  sh_island_t new_island_exit;
  size_t new_island_exit_size = (!addr_info->is_proc_start || is_to_interceptor) ? 20 : 16;
  uint32_t new_exit[6];

  // alloc an island-exit (exit jump to island-exit)
  uintptr_t island_exit_range_low = pc > SH_INST_A64_B_OFFSET_LOW ? pc - SH_INST_A64_B_OFFSET_LOW : 0;
  uintptr_t island_exit_range_high =
      UINTPTR_MAX - pc > SH_INST_A64_B_OFFSET_HIGH ? pc + SH_INST_A64_B_OFFSET_HIGH : UINTPTR_MAX;
  sh_island_alloc(&new_island_exit, new_island_exit_size, island_exit_range_low, island_exit_range_high, pc,
                  addr_info);
  if (0 == new_island_exit.addr) return SHADOWHOOK_ERRNO_HOOK_ISLAND_EXIT;

  // absolute jump to new_addr in island-exit
  if (!addr_info->is_proc_start || is_to_interceptor) {
    sh_a64_absolute_jump_with_br_rx((uint32_t *)new_island_exit.addr, new_addr);
  } else {
    sh_a64_absolute_jump_with_br_ip((uint32_t *)new_island_exit.addr, new_addr);
  }
  sh_util_clear_cache(new_island_exit.addr, new_island_exit.size);

  // relative jump to the island-exit by overwriting the head of original function
  sh_a64_relative_jump(new_exit, new_island_exit.addr, pc);
  if (0 != (r = sh_util_write_inst(target_addr, new_exit, self->backup_len))) {
    sh_island_free(&new_island_exit);
    return r;
  }

  // OK
  if (0 != self->island_exit.addr) sh_island_free(&self->island_exit);
  self->island_exit = new_island_exit;
  memcpy(self->exit, new_exit, self->backup_len);

  if (addr_info->is_proc_start) {
    SH_LOG_INFO("a64: %shook (with island) OK. target %" PRIxPTR " -> island-exit %" PRIxPTR
                " -> new %" PRIxPTR " -> enter %" PRIxPTR " -> resume %" PRIxPTR,
                is_rehook ? "re-" : "", target_addr, self->island_exit.addr, new_addr, self->enter,
                target_addr + self->backup_len);
  } else {
    SH_LOG_INFO("a64: %shook (with island) OK. target %" PRIxPTR " -> island-exit %" PRIxPTR
                " -> new %" PRIxPTR " -> enter %" PRIxPTR " -> island-enter %" PRIxPTR " -> resume %" PRIxPTR,
                is_rehook ? "re-" : "", target_addr, self->island_exit.addr, new_addr, self->enter,
                self->island_enter.addr, target_addr + self->backup_len);
  }
  return 0;
}

static int sh_inst_hook_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                    uintptr_t new_addr, bool is_to_interceptor,
                                    sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  int r;
  if (0 != (r = sh_inst_rewrite_with_island(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg)))
    return r;
  if (0 !=
      (r = sh_inst_reloc_with_island(self, target_addr, addr_info, new_addr, is_to_interceptor, false))) {
    if (0 != self->island_enter.addr) sh_island_free(&self->island_enter);
    if (0 != self->island_rewrite.addr) sh_island_free(&self->island_rewrite);
    return r;
  }
  return 0;
}
#endif

#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
static int sh_inst_rewrite_without_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                          sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  uintptr_t resume_addr;
  if (!addr_info->is_proc_start) {
    self->backup_len = 24;
    resume_addr = target_addr + self->backup_len - 4;
  } else {
    self->backup_len = 20;
    resume_addr = target_addr + self->backup_len;
  }

  if (addr_info->is_sym_addr) {
    size_t resume_len = 0;
    if (!addr_info->is_proc_start) {
      if (NULL != addr_info->dli_saddr && (uintptr_t)addr_info->dli_saddr <= target_addr &&
          target_addr < (uintptr_t)addr_info->dli_saddr + addr_info->dli_ssize) {
        resume_len = addr_info->dli_ssize - (target_addr - (uintptr_t)addr_info->dli_saddr);
      }
    } else {
      if (NULL != addr_info->dli_saddr) {
        resume_len = addr_info->dli_ssize;
      }
    }
    if (resume_len < self->backup_len) return SHADOWHOOK_ERRNO_HOOK_SYMSZ;
  }

  return sh_inst_safe_rewrite(self, target_addr, addr_info, resume_addr, set_orig_addr, set_orig_addr_arg);
}

static int sh_inst_reloc_without_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                        uintptr_t new_addr, bool is_to_interceptor, bool is_rehook) {
  uint32_t new_exit[6];

  if (!addr_info->is_proc_start) {
    // backup_len == 24
    sh_a64_absolute_jump_with_br_rx(new_exit, new_addr);
    sh_a64_restore_rx((uint32_t *)((uintptr_t)new_exit + 20));
  } else {
    // backup_len == 20
    if (is_to_interceptor) {
      sh_a64_absolute_jump_with_br_rx(new_exit, new_addr);
    } else {
      // To ensure that rehooking works correctly, the length of
      // the rewritten instructions must always be the same.
      sh_a64_nop(new_exit);
      sh_a64_absolute_jump_with_br_ip((uint32_t *)((uintptr_t)new_exit + 4), new_addr);
    }
  }

  int r;
  if (0 != (r = sh_util_write_inst(target_addr, new_exit, self->backup_len))) return r;
  memcpy(self->exit, new_exit, self->backup_len);

  SH_LOG_INFO("a64: %shook (without island) OK. target %" PRIxPTR " -> new %" PRIxPTR " -> enter %" PRIxPTR
              " -> resume %" PRIxPTR,
              is_rehook ? "re-" : "", target_addr, new_addr, self->enter,
              addr_info->is_proc_start ? target_addr + self->backup_len : target_addr + self->backup_len - 4);

  return 0;
}

static int sh_inst_hook_without_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                       uintptr_t new_addr, bool is_to_interceptor,
                                       sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  int r;
  if (0 !=
      (r = sh_inst_rewrite_without_island(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg)))
    return r;
  if (0 !=
      (r = sh_inst_reloc_without_island(self, target_addr, addr_info, new_addr, is_to_interceptor, false)))
    return r;
  return 0;
}
#endif

int sh_inst_hook(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                 bool is_to_interceptor, sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  self->enter = sh_enter_alloc();
  if (0 == self->enter) return SHADOWHOOK_ERRNO_HOOK_ENTER;

  int r = -1;
#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND
  if (0 == (r = sh_inst_hook_with_island(self, target_addr, addr_info, new_addr, is_to_interceptor,
                                         set_orig_addr, set_orig_addr_arg)))
    return r;
#endif

#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
  if (NULL == addr_info->dli_saddr && addr_info->is_sym_addr) {
    if (0 != (r = sh_linker_get_addr_info_by_addr((void *)target_addr, addr_info->is_sym_addr,
                                                  addr_info->is_proc_start, addr_info, false, NULL, 0)))
      goto err;
  }
  if (0 == (r = sh_inst_hook_without_island(self, target_addr, addr_info, new_addr, is_to_interceptor,
                                            set_orig_addr, set_orig_addr_arg)))
    return r;

err:
#endif

  // hook failed
  if (NULL != set_orig_addr) set_orig_addr(0, set_orig_addr_arg);
  sh_enter_free(self->enter);
  return r;
}

int sh_inst_rehook(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                   bool is_to_interceptor) {
  if (4 == self->backup_len) {
#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND
    return sh_inst_reloc_with_island(self, target_addr, addr_info, new_addr, is_to_interceptor, true);
#else
    abort();
#endif
  } else {
#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
    return sh_inst_reloc_without_island(self, target_addr, addr_info, new_addr, is_to_interceptor, true);
#else
    abort();
#endif
  }
}

int sh_inst_unhook(sh_inst_t *self, uintptr_t target_addr) {
  int r;

  // restore the instructions at the target address
  SH_SIG_TRY(SIGSEGV, SIGBUS) {
    r = memcmp((void *)target_addr, self->exit, self->backup_len);
  }
  SH_SIG_CATCH() {
    return SHADOWHOOK_ERRNO_UNHOOK_CMP_CRASH;
  }
  SH_SIG_EXIT
  if (0 != r) return SHADOWHOOK_ERRNO_UNHOOK_TRAMPO_MISMATCH;
  if (0 != (r = sh_util_write_inst(target_addr, self->backup, self->backup_len))) return r;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);

  // free memory space for island-exit and island-enter
  if (0 != self->island_exit.addr) sh_island_free(&self->island_exit);
  if (0 != self->island_enter.addr) sh_island_free(&self->island_enter);
  if (0 != self->island_rewrite.addr) sh_island_free(&self->island_rewrite);

  // free memory space for enter
  sh_enter_free(self->enter);

  SH_LOG_INFO("a64: unhook OK. target %" PRIxPTR, target_addr);
  return 0;
}

void sh_inst_free_after_dlclose(sh_inst_t *self, uintptr_t target_addr) {
  // free memory space for island-exit and island-enter
  if (0 != self->island_exit.addr) sh_island_free_after_dlclose(&self->island_exit);
  if (0 != self->island_enter.addr) sh_island_free_after_dlclose(&self->island_enter);

  // free memory space for enter
  sh_enter_free(self->enter);

  SH_LOG_INFO("a64: free_after_dlclose OK. target %" PRIxPTR, target_addr);
}

extern void shadowhook_interceptor_glue(void);
void sh_inst_build_glue_launcher(void *buf, void *ctx) {
  uint32_t *b = (uint32_t *)buf;
#ifdef SH_CONFIG_CORRUPT_IP_REGS
  b[0] = 0x58000070;  // LDR X16, #12
  b[1] = 0x58000091;  // LDR X17, #16
  b[2] = 0xd61f0200;  // BR X16
#else
  b[0] = 0x58000060;  // LDR X0, #12
  b[1] = 0x58000081;  // LDR X1, #16
  b[2] = 0xd61f0000;  // BR X0
#endif
  b[3] = (uintptr_t)shadowhook_interceptor_glue & 0xFFFFFFFF;
  b[4] = (uintptr_t)shadowhook_interceptor_glue >> 32u;
  b[5] = (uintptr_t)ctx & 0xFFFFFFFF;
  b[6] = (uintptr_t)ctx >> 32u;
}
