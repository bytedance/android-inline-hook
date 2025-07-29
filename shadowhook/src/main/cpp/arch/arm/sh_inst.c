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

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "sh_a32.h"
#include "sh_config.h"
#include "sh_enter.h"
#include "sh_island.h"
#include "sh_linker.h"
#include "sh_log.h"
#include "sh_sig.h"
#include "sh_t16.h"
#include "sh_t32.h"
#include "sh_txx.h"
#include "sh_util.h"
#include "shadowhook.h"
#include "xdl.h"

static void sh_inst_thumb_get_rewrite_info(sh_inst_t *self, uintptr_t target_addr,
                                           sh_txx_rewrite_info_t *rinfo) {
  memset(rinfo, 0, sizeof(sh_txx_rewrite_info_t));

  size_t idx = 0;
  uintptr_t target_addr_offset = 0;
  uintptr_t pc = target_addr + 4;
  size_t rewrite_len = 0;

  while (rewrite_len < self->backup_len) {
    // IT block
    sh_t16_it_t it;
    if (sh_t16_parse_it(&it, *((uint16_t *)(target_addr + target_addr_offset)), pc)) {
      rewrite_len += (2 + it.insts_len);

      size_t it_block_idx = idx++;
      size_t it_block_len = 4 + 4;  // IT-else + IT-then
      for (size_t i = 0, j = 0; i < it.insts_cnt; i++) {
        bool is_thumb32 = sh_util_is_thumb32((uintptr_t)(&(it.insts[j])));
        if (is_thumb32) {
          it_block_len += sh_t32_get_rewrite_inst_len(it.insts[j], it.insts[j + 1]);
          rinfo->inst_lens[idx++] = 0;
          rinfo->inst_lens[idx++] = 0;
          j += 2;
        } else {
          it_block_len += sh_t16_get_rewrite_inst_len(it.insts[j]);
          rinfo->inst_lens[idx++] = 0;
          j += 1;
        }
      }
      rinfo->inst_lens[it_block_idx] = it_block_len;

      target_addr_offset += (2 + it.insts_len);
      pc += (2 + it.insts_len);
    }
    // not IT block
    else {
      bool is_thumb32 = sh_util_is_thumb32(target_addr + target_addr_offset);
      size_t inst_len = (is_thumb32 ? 4 : 2);
      rewrite_len += inst_len;

      if (is_thumb32) {
        rinfo->inst_lens[idx++] =
            sh_t32_get_rewrite_inst_len(*((uint16_t *)(target_addr + target_addr_offset)),
                                        *((uint16_t *)(target_addr + target_addr_offset + 2)));
        rinfo->inst_lens[idx++] = 0;
      } else
        rinfo->inst_lens[idx++] =
            sh_t16_get_rewrite_inst_len(*((uint16_t *)(target_addr + target_addr_offset)));

      target_addr_offset += inst_len;
      pc += inst_len;
    }
  }

  rinfo->start_addr = target_addr;
  rinfo->end_addr = target_addr + rewrite_len;
  rinfo->buf = (uint16_t *)self->enter;
  rinfo->buf_offset = 0;
  rinfo->inst_prolog_len = 0;
  rinfo->inst_lens_cnt = idx;
}

static int sh_inst_thumb_rewrite(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                 sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  // backup original instructions (length: 4 or 8 or 10)
  memcpy((void *)(self->backup), (void *)target_addr, self->backup_len);

  // package the information passed to rewrite
  sh_txx_rewrite_info_t rinfo;
  sh_inst_thumb_get_rewrite_info(self, target_addr, &rinfo);

  if (!addr_info->is_proc_start) {
    rinfo.buf_offset += sh_t32_restore_ip((uint16_t *)self->enter);
    rinfo.inst_prolog_len = rinfo.buf_offset;
  }

  // rewrite original instructions
  uintptr_t target_addr_offset = 0;
  uintptr_t pc = target_addr + 4;
  self->rewritten_len = 0;
  while (self->rewritten_len < self->backup_len) {
    // IT block
    sh_t16_it_t it;
    if (sh_t16_parse_it(&it, *((uint16_t *)(target_addr + target_addr_offset)), pc)) {
      self->rewritten_len += (2 + it.insts_len);

      // save space holder point of IT-else B instruction
      uintptr_t enter_inst_else_p = self->enter + rinfo.buf_offset;
      rinfo.buf_offset += 2;  // B<c> <label>
      rinfo.buf_offset += 2;  // NOP

      // rewrite IT block
      size_t enter_inst_else_len = 4;  // B<c> + NOP + B + NOP
      size_t enter_inst_then_len = 0;  // B + NOP
      uintptr_t enter_inst_then_p = 0;
      for (size_t i = 0, j = 0; i < it.insts_cnt; i++) {
        if (i == it.insts_else_cnt) {
          // save space holder point of IT-then (for B instruction)
          enter_inst_then_p = self->enter + rinfo.buf_offset;
          rinfo.buf_offset += 2;  // B <label>
          rinfo.buf_offset += 2;  // NOP

          // fill IT-else B instruction
          sh_t16_rewrite_it_else((uint16_t *)enter_inst_else_p, (uint16_t)enter_inst_else_len, &it);
        }

        // rewrite instructions in IT block
        bool is_thumb32 = sh_util_is_thumb32((uintptr_t)(&(it.insts[j])));
        size_t len;
        if (is_thumb32)
          len = sh_t32_rewrite((uint16_t *)(self->enter + rinfo.buf_offset), it.insts[j], it.insts[j + 1],
                               it.pcs[i], &rinfo);
        else
          len = sh_t16_rewrite((uint16_t *)(self->enter + rinfo.buf_offset), it.insts[j], it.pcs[i], &rinfo);
        if (0 == len) return SHADOWHOOK_ERRNO_HOOK_REWRITE_FAILED;
        rinfo.buf_offset += len;
        j += (is_thumb32 ? 2 : 1);

        // save the total offset for ELSE/THEN in enter
        if (i < it.insts_else_cnt)
          enter_inst_else_len += len;
        else
          enter_inst_then_len += len;

        if (i == it.insts_cnt - 1) {
          // fill IT-then B instruction
          sh_t16_rewrite_it_then((uint16_t *)enter_inst_then_p, (uint16_t)enter_inst_then_len);
        }
      }

      target_addr_offset += (2 + it.insts_len);
      pc += (2 + it.insts_len);
    }
    // not IT block
    else {
      bool is_thumb32 = sh_util_is_thumb32(target_addr + target_addr_offset);
      size_t inst_len = (is_thumb32 ? 4 : 2);
      self->rewritten_len += inst_len;

      // rewrite original instructions (fill in enter)
      SH_LOG_INFO("thumb rewrite: offset %zu, pc %" PRIxPTR, rinfo.buf_offset, pc);
      size_t len;
      if (is_thumb32)
        len = sh_t32_rewrite((uint16_t *)(self->enter + rinfo.buf_offset),
                             *((uint16_t *)(target_addr + target_addr_offset)),
                             *((uint16_t *)(target_addr + target_addr_offset + 2)), pc, &rinfo);
      else
        len = sh_t16_rewrite((uint16_t *)(self->enter + rinfo.buf_offset),
                             *((uint16_t *)(target_addr + target_addr_offset)), pc, &rinfo);
      if (0 == len) return SHADOWHOOK_ERRNO_HOOK_REWRITE_FAILED;
      rinfo.buf_offset += len;

      target_addr_offset += inst_len;
      pc += inst_len;
    }
  }
  SH_LOG_INFO("thumb rewrite: len %zu to %zu", self->rewritten_len, rinfo.buf_offset);

  // absolute jump back to remaining original instructions (fill in enter)
  uintptr_t resume_addr = SH_UTIL_SET_BIT0(target_addr + self->rewritten_len);
  rinfo.buf_offset += sh_t32_absolute_jump((uint16_t *)(self->enter + rinfo.buf_offset), true, resume_addr);
  sh_util_clear_cache(self->enter, rinfo.buf_offset);

  // save original function address
  if (NULL != set_orig_addr) set_orig_addr(SH_UTIL_SET_BIT0(self->enter), set_orig_addr_arg);
  return 0;
}

static int sh_inst_thumb_safe_rewrite(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                      sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  size_t resume_len = 26;  // thumb max rewritten_len
  if (addr_info->is_sym_addr) {
    resume_len = addr_info->dli_ssize - (target_addr - SH_UTIL_CLEAR_BIT0((uintptr_t)addr_info->dli_saddr));
  }
  if (0 != sh_util_mprotect(target_addr, resume_len, PROT_READ | PROT_WRITE | PROT_EXEC))
    return SHADOWHOOK_ERRNO_MPROT;

  int r;
  SH_SIG_TRY(SIGSEGV, SIGBUS) {
    r = sh_inst_thumb_rewrite(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg);
  }
  SH_SIG_CATCH() {
    return SHADOWHOOK_ERRNO_HOOK_REWRITE_CRASH;
  }
  SH_SIG_EXIT
  return r;
}

static bool sh_inst_thumb_is_long_enough(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info) {
  if (!addr_info->is_sym_addr) return true;

  uintptr_t dli_saddr = SH_UTIL_CLEAR_BIT0((uintptr_t)addr_info->dli_saddr);
  size_t resume_len = 0;
  if (!addr_info->is_proc_start) {
    if (0 != dli_saddr && dli_saddr <= target_addr && target_addr < dli_saddr + addr_info->dli_ssize) {
      resume_len = addr_info->dli_ssize - (target_addr - dli_saddr);
    }
  } else {
    if (0 != dli_saddr) {
      resume_len = addr_info->dli_ssize;
    }
  }
  if (resume_len >= self->backup_len) return true;

#ifdef SH_CONFIG_DETECT_THUMB_TAIL_ALIGNED
  // check align-4 in the end of symbol
  if ((self->backup_len == resume_len + 2) && ((target_addr + resume_len) % 4 == 2)) {
    uintptr_t sym_end = dli_saddr + addr_info->dli_ssize;
    if (0 != sh_util_mprotect(sym_end, 2, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;

    // should be zero-ed
    if (0 != *((uint16_t *)sym_end)) return false;

    // should not belong to any symbol
    void *dlcache = NULL;
    xdl_info_t dlinfo;
    if (sh_util_get_api_level() >= __ANDROID_API_L__) {
      xdl_addr((void *)SH_UTIL_SET_BIT0(sym_end), &dlinfo, &dlcache);
    } else {
      SH_SIG_TRY(SIGSEGV, SIGBUS) {
        xdl_addr((void *)SH_UTIL_SET_BIT0(sym_end), &dlinfo, &dlcache);
      }
      SH_SIG_CATCH() {
        memset(&dlinfo, 0, sizeof(dlinfo));
        SH_LOG_WARN("thumb detect tail aligned: crashed");
      }
      SH_SIG_EXIT
    }
    xdl_addr_clean(&dlcache);
    if (0 != dlinfo.dli_saddr) return false;

    // trust here is useless alignment data
    SH_LOG_INFO("thumb detect tail aligned: OK %" PRIxPTR, target_addr);
    return true;
  }
#endif

  return false;
}

#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND

// B T4: [-16M, +16M - 2]
#define SH_INST_T32_B_RANGE_LOW  (16777216)
#define SH_INST_T32_B_RANGE_HIGH (16777214)

static int sh_inst_thumb_rewrite_with_island(sh_inst_t *self, uintptr_t target_addr,
                                             sh_addr_info_t *addr_info, sh_inst_set_orig_addr_t set_orig_addr,
                                             void *set_orig_addr_arg) {
  self->backup_len = 4;
  if (!sh_inst_thumb_is_long_enough(self, target_addr, addr_info)) return SHADOWHOOK_ERRNO_HOOK_SYMSZ;

  return sh_inst_thumb_safe_rewrite(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg);
}

static int sh_inst_thumb_reloc_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                           uintptr_t new_addr, bool is_rehook) {
  int r;
  uintptr_t pc = target_addr + 4;
  sh_island_t new_island_exit;
  uint32_t new_exit[3];

  // alloc an island-exit (exit jump to island-exit)
  uintptr_t island_exit_range_low = pc > SH_INST_T32_B_RANGE_LOW ? pc - SH_INST_T32_B_RANGE_LOW : 0;
  uintptr_t island_exit_range_high =
      UINTPTR_MAX - pc > SH_INST_T32_B_RANGE_HIGH ? pc + SH_INST_T32_B_RANGE_HIGH : UINTPTR_MAX;
  sh_island_alloc(&new_island_exit, 8, island_exit_range_low, island_exit_range_high, pc, addr_info);
  if (0 == new_island_exit.addr) return SHADOWHOOK_ERRNO_HOOK_ISLAND_EXIT;

  // absolute jump to new_addr in island-exit
  sh_t32_absolute_jump((uint16_t *)new_island_exit.addr, true, new_addr);
  sh_util_clear_cache(new_island_exit.addr, new_island_exit.size);

  // relative jump to the island-exit by overwriting the head of original function
  sh_t32_relative_jump((uint16_t *)new_exit, new_island_exit.addr, pc);
  if (0 != (r = sh_util_write_inst(target_addr, new_exit, self->backup_len))) {
    sh_island_free(&new_island_exit);
    return r;
  }

  // OK
  if (0 != self->island_exit.addr) sh_island_free(&self->island_exit);
  self->island_exit = new_island_exit;
  memcpy(self->exit, new_exit, self->backup_len);

  SH_LOG_INFO("thumb: %shook (with island) OK. target %" PRIxPTR " -> island-exit %" PRIxPTR
              " -> new %" PRIxPTR " -> enter %" PRIxPTR " -> resume %" PRIxPTR,
              is_rehook ? "re-" : "", SH_UTIL_SET_BIT0(target_addr), self->island_exit.addr, new_addr,
              SH_UTIL_SET_BIT0(self->enter), SH_UTIL_SET_BIT0(target_addr + self->rewritten_len));
  return 0;
}

static int sh_inst_thumb_hook_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                          uintptr_t new_addr, sh_inst_set_orig_addr_t set_orig_addr,
                                          void *set_orig_addr_arg) {
  int r;
  if (0 !=
      (r = sh_inst_thumb_rewrite_with_island(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg)))
    return r;
  if (0 != (r = sh_inst_thumb_reloc_with_island(self, target_addr, addr_info, new_addr, false))) return r;
  return 0;
}
#endif

#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND

static int sh_inst_thumb_rewrite_without_island(sh_inst_t *self, uintptr_t target_addr,
                                                sh_addr_info_t *addr_info,
                                                sh_inst_set_orig_addr_t set_orig_addr,
                                                void *set_orig_addr_arg) {
  bool is_align4 = (0 == (target_addr % 4));
  self->backup_len = (is_align4 ? 8 : 10);
  if (!sh_inst_thumb_is_long_enough(self, target_addr, addr_info)) return SHADOWHOOK_ERRNO_HOOK_SYMSZ;

  return sh_inst_thumb_safe_rewrite(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg);
}

static int sh_inst_thumb_reloc_without_island(sh_inst_t *self, uintptr_t target_addr, uintptr_t new_addr,
                                              bool is_rehook) {
  bool is_align4 = (0 == (target_addr % 4));
  uint32_t new_exit[3];
  int r;

  sh_t32_absolute_jump((uint16_t *)new_exit, is_align4, new_addr);
  if (0 != (r = sh_util_write_inst(target_addr, new_exit, self->backup_len))) return r;
  memcpy(self->exit, new_exit, self->backup_len);

  SH_LOG_INFO("thumb: %shook (without island) OK. target %" PRIxPTR " -> new %" PRIxPTR " -> enter %" PRIxPTR
              " -> resume %" PRIxPTR,
              is_rehook ? "re-" : "", SH_UTIL_SET_BIT0(target_addr), new_addr, SH_UTIL_SET_BIT0(self->enter),
              SH_UTIL_SET_BIT0(target_addr + self->rewritten_len));
  return 0;
}

static int sh_inst_thumb_hook_without_island(sh_inst_t *self, uintptr_t target_addr,
                                             sh_addr_info_t *addr_info, uintptr_t new_addr,
                                             sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  int r;
  if (0 != (r = sh_inst_thumb_rewrite_without_island(self, target_addr, addr_info, set_orig_addr,
                                                     set_orig_addr_arg)))
    return r;
  if (0 != (r = sh_inst_thumb_reloc_without_island(self, target_addr, new_addr, false))) return r;
  return 0;
}
#endif

static int sh_inst_arm_rewrite(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                               sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  // backup original instructions (length: 4 or 8)
  memcpy((void *)(self->backup), (void *)target_addr, self->backup_len);

  // package the information passed to rewrite
  sh_a32_rewrite_info_t rinfo;
  rinfo.start_addr = target_addr;
  rinfo.end_addr = target_addr + self->backup_len;
  rinfo.buf = (uint32_t *)self->enter;
  rinfo.buf_offset = 0;
  rinfo.inst_prolog_len = 0;
  rinfo.inst_lens_cnt = self->backup_len / 4;
  for (uintptr_t i = 0; i < self->backup_len; i += 4)
    rinfo.inst_lens[i / 4] = sh_a32_get_rewrite_inst_len(*((uint32_t *)(target_addr + i)));

  if (!addr_info->is_proc_start) {
    rinfo.buf_offset += sh_a32_restore_ip((uint32_t *)self->enter);
    rinfo.inst_prolog_len = rinfo.buf_offset;
  }

  // rewrite original instructions (fill in enter)
  uintptr_t pc = target_addr + 8;
  for (uintptr_t i = 0; i < self->backup_len; i += 4, pc += 4) {
    size_t offset = sh_a32_rewrite((uint32_t *)(self->enter + rinfo.buf_offset),
                                   *((uint32_t *)(target_addr + i)), pc, &rinfo);
    if (0 == offset) return SHADOWHOOK_ERRNO_HOOK_REWRITE_FAILED;
    rinfo.buf_offset += offset;
  }

  // absolute jump back to remaining original instructions (fill in enter)
  uintptr_t resume_addr = target_addr + self->backup_len;
  rinfo.buf_offset += sh_a32_absolute_jump((uint32_t *)(self->enter + rinfo.buf_offset), resume_addr);
  sh_util_clear_cache(self->enter, rinfo.buf_offset);

  // save original function address
  if (NULL != set_orig_addr) set_orig_addr(self->enter, set_orig_addr_arg);
  return 0;
}

static int sh_inst_arm_safe_rewrite(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                    sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  if (0 != sh_util_mprotect(target_addr, self->backup_len, PROT_READ | PROT_WRITE | PROT_EXEC))
    return SHADOWHOOK_ERRNO_MPROT;

  int r;
  SH_SIG_TRY(SIGSEGV, SIGBUS) {
    r = sh_inst_arm_rewrite(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg);
  }
  SH_SIG_CATCH() {
    return SHADOWHOOK_ERRNO_HOOK_REWRITE_CRASH;
  }
  SH_SIG_EXIT
  return r;
}

#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND

// B A1: [-32M, +32M - 4]
#define SH_INST_A32_B_RANGE_LOW  (33554432)
#define SH_INST_A32_B_RANGE_HIGH (33554428)

static int sh_inst_arm_rewrite_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                           sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  self->backup_len = 4;

  return sh_inst_arm_safe_rewrite(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg);
}

static int sh_inst_arm_reloc_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                         uintptr_t new_addr, bool is_rehook) {
  int r;
  uintptr_t pc = target_addr + 8;
  sh_island_t new_island_exit;
  uint32_t new_exit[3];

  // alloc an island-exit (exit jump to island-exit)
  uintptr_t island_exit_range_low = pc > SH_INST_A32_B_RANGE_LOW ? pc - SH_INST_A32_B_RANGE_LOW : 0;
  uintptr_t island_exit_range_high =
      UINTPTR_MAX - pc > SH_INST_A32_B_RANGE_HIGH ? pc + SH_INST_A32_B_RANGE_HIGH : UINTPTR_MAX;
  sh_island_alloc(&new_island_exit, 8, island_exit_range_low, island_exit_range_high, pc, addr_info);
  if (0 == new_island_exit.addr) return SHADOWHOOK_ERRNO_HOOK_ISLAND_EXIT;

  // absolute jump to new_addr in island-exit
  sh_a32_absolute_jump((uint32_t *)new_island_exit.addr, new_addr);
  sh_util_clear_cache(new_island_exit.addr, new_island_exit.size);

  // relative jump to the island-exit by overwriting the head of original function
  sh_a32_relative_jump((uint32_t *)new_exit, new_island_exit.addr, pc);
  if (0 != (r = sh_util_write_inst(target_addr, new_exit, self->backup_len))) {
    sh_island_free(&new_island_exit);
    return r;
  }

  // OK
  if (0 != self->island_exit.addr) sh_island_free(&self->island_exit);
  self->island_exit = new_island_exit;
  memcpy(self->exit, new_exit, self->backup_len);

  SH_LOG_INFO("a32: %shook (with island) OK. target %" PRIxPTR " -> island-exit %" PRIxPTR " -> new %" PRIxPTR
              " -> enter %" PRIxPTR " -> resume %" PRIxPTR,
              is_rehook ? "re-" : "", target_addr, self->island_exit.addr, new_addr, self->enter,
              target_addr + self->backup_len);
  return 0;
}

static int sh_inst_arm_hook_with_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                        uintptr_t new_addr, sh_inst_set_orig_addr_t set_orig_addr,
                                        void *set_orig_addr_arg) {
  int r;
  if (0 !=
      (r = sh_inst_arm_rewrite_with_island(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg)))
    return r;
  if (0 != (r = sh_inst_arm_reloc_with_island(self, target_addr, addr_info, new_addr, false))) return r;
  return 0;
}
#endif

#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND

static int sh_inst_arm_rewrite_without_island(sh_inst_t *self, uintptr_t target_addr,
                                              sh_addr_info_t *addr_info,
                                              sh_inst_set_orig_addr_t set_orig_addr,
                                              void *set_orig_addr_arg) {
  self->backup_len = 8;

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

  return sh_inst_arm_safe_rewrite(self, target_addr, addr_info, set_orig_addr, set_orig_addr_arg);
}

static int sh_inst_arm_reloc_without_island(sh_inst_t *self, uintptr_t target_addr, uintptr_t new_addr,
                                            bool is_rehook) {
  uint32_t new_exit[3];
  int r;

  sh_a32_absolute_jump((uint32_t *)new_exit, new_addr);
  if (0 != (r = sh_util_write_inst(target_addr, new_exit, self->backup_len))) return r;
  memcpy(self->exit, new_exit, self->backup_len);

  SH_LOG_INFO("a32: %shook (without island) OK. target %" PRIxPTR " -> new %" PRIxPTR " -> enter %" PRIxPTR
              " -> resume %" PRIxPTR,
              is_rehook ? "re-" : "", target_addr, new_addr, self->enter, target_addr + self->backup_len);
  return 0;
}

static int sh_inst_arm_hook_without_island(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info,
                                           uintptr_t new_addr, sh_inst_set_orig_addr_t set_orig_addr,
                                           void *set_orig_addr_arg) {
  int r;
  if (0 != (r = sh_inst_arm_rewrite_without_island(self, target_addr, addr_info, set_orig_addr,
                                                   set_orig_addr_arg)))
    return r;
  if (0 != (r = sh_inst_arm_reloc_without_island(self, target_addr, new_addr, false))) return r;
  return 0;
}
#endif

int sh_inst_hook(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                 bool is_to_interceptor, sh_inst_set_orig_addr_t set_orig_addr, void *set_orig_addr_arg) {
  (void)is_to_interceptor;

  self->enter = sh_enter_alloc();
  if (0 == self->enter) return SHADOWHOOK_ERRNO_HOOK_ENTER;

  int r = -1;
  if (SH_UTIL_IS_THUMB(target_addr)) {
    if (NULL == addr_info->dli_saddr && addr_info->is_sym_addr) {
      if (0 != (r = sh_linker_get_addr_info_by_addr((void *)target_addr, addr_info->is_sym_addr,
                                                    addr_info->is_proc_start, addr_info, false, NULL, 0)))
        goto err;
    }
    target_addr = SH_UTIL_CLEAR_BIT0(target_addr);
#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND
    if (0 == (r = sh_inst_thumb_hook_with_island(self, target_addr, addr_info, new_addr, set_orig_addr,
                                                 set_orig_addr_arg)))
      return r;
#endif
#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
    if (0 == (r = sh_inst_thumb_hook_without_island(self, target_addr, addr_info, new_addr, set_orig_addr,
                                                    set_orig_addr_arg)))
      return r;
#endif
  } else {
#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND
    if (0 == (r = sh_inst_arm_hook_with_island(self, target_addr, addr_info, new_addr, set_orig_addr,
                                               set_orig_addr_arg)))
      return r;
#endif
#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
    if (NULL == addr_info->dli_saddr && addr_info->is_sym_addr) {
      if (0 != (r = sh_linker_get_addr_info_by_addr((void *)target_addr, addr_info->is_sym_addr,
                                                    addr_info->is_proc_start, addr_info, false, NULL, 0)))
        goto err;
    }
    if (0 == (r = sh_inst_arm_hook_without_island(self, target_addr, addr_info, new_addr, set_orig_addr,
                                                  set_orig_addr_arg)))
      return r;
#endif
  }

err:
  // hook failed
  if (NULL != set_orig_addr) set_orig_addr(0, set_orig_addr_arg);
  sh_enter_free(self->enter);
  return r;
}

int sh_inst_rehook(sh_inst_t *self, uintptr_t target_addr, sh_addr_info_t *addr_info, uintptr_t new_addr,
                   bool is_to_interceptor) {
  (void)is_to_interceptor;

  if (SH_UTIL_IS_THUMB(target_addr)) {
    target_addr = SH_UTIL_CLEAR_BIT0(target_addr);
    if (4 == self->backup_len) {
#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND
      return sh_inst_thumb_reloc_with_island(self, target_addr, addr_info, new_addr, true);
#else
      abort();
#endif
    } else {
#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
      (void)addr_info;
      return sh_inst_thumb_reloc_without_island(self, target_addr, new_addr, true);
#else
      abort();
#endif
    }
  } else {
    if (4 == self->backup_len) {
#ifdef SH_CONFIG_TRY_HOOK_WITH_ISLAND
      return sh_inst_arm_reloc_with_island(self, target_addr, addr_info, new_addr, true);
#else
      abort();
#endif
    } else {
#ifdef SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND
      (void)addr_info;
      return sh_inst_arm_reloc_without_island(self, target_addr, new_addr, true);
#else
      abort();
#endif
    }
  }
}

int sh_inst_unhook(sh_inst_t *self, uintptr_t target_addr) {
  int r;
  bool is_thumb = SH_UTIL_IS_THUMB(target_addr);
  if (is_thumb) target_addr = SH_UTIL_CLEAR_BIT0(target_addr);

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

  // free memory space for island-exit
  if (0 != self->island_exit.addr) sh_island_free(&self->island_exit);

  // free memory space for enter
  sh_enter_free(self->enter);

  SH_LOG_INFO("%s: unhook OK. target %" PRIxPTR, is_thumb ? "thumb" : "a32", target_addr);
  return 0;
}

void sh_inst_free_after_dlclose(sh_inst_t *self, uintptr_t target_addr) {
  // free memory space for island-exit
  if (0 != self->island_exit.addr) sh_island_free_after_dlclose(&self->island_exit);

  // free memory space for enter
  sh_enter_free(self->enter);

  bool is_thumb = SH_UTIL_IS_THUMB(target_addr);
  SH_LOG_INFO("%s: free_after_dlclose OK. target %" PRIxPTR, is_thumb ? "thumb" : "a32", target_addr);
}

extern void shadowhook_interceptor_glue(void);
extern void shadowhook_interceptor_glue_vfpv3d16(void);
extern void shadowhook_interceptor_glue_vfpv3d32(void);
void sh_inst_build_glue_launcher(void *buf, void *ctx) {
  uint32_t *b = (uint32_t *)buf;
  // instruction sets: arm
#ifdef SH_CONFIG_CORRUPT_IP_REGS
  b[0] = 0xE50DC004;  // STR IP, [SP, #-4]
  b[1] = 0xE59FC004;  // LDR IP, [PC, #4]
  b[2] = 0xE51FF004;  // LDR PC, [PC, #-4]
#else
  b[0] = 0xE50D0004;  // STR R0, [SP, #-4]
  b[1] = 0xE59F0004;  // LDR R0, [PC, #4]
  b[2] = 0xE51FF004;  // LDR PC, [PC, #-4]
#endif
  size_t cpu_feat = sh_util_get_arm_cpu_features();
  if (cpu_feat & SH_UTIL_ARM_CPU_FEATURE_VFPV3D32)
    b[3] = (uint32_t)shadowhook_interceptor_glue_vfpv3d32;
  else if (cpu_feat & SH_UTIL_ARM_CPU_FEATURE_VFPV3D16)
    b[3] = (uint32_t)shadowhook_interceptor_glue_vfpv3d16;
  else
    b[3] = (uint32_t)shadowhook_interceptor_glue;
  b[4] = (uint32_t)ctx;
}
