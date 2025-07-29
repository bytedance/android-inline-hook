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

// Created by Kelun Cai (caikelun@bytedance.com) on 2025-03-06.

#include "sh_config.h"

#define ENTRY(f)      \
  .globl f;           \
  .arm;               \
  .balign 4;          \
  .text;              \
  .type f, #function; \
f:                    \
  .fnstart;           \
  .cfi_startproc

#define END(f)   \
  .cfi_endproc;  \
  .size f, .- f; \
  .fnend

#ifdef SH_CONFIG_CORRUPT_IP_REGS
#define IP_0 ip
#else
#define IP_0 r0
#endif

// [[ CPU context struct ]]
// --------------------------------------------
// struct cpu_context {
//   uint32_t regs[16];  // .size = 0x40
//   uint32_t cpsr;
//   uint32_t fpscr;
//   __uint128_t vregs[16];  // .size = 0x100
// };

// [[ stack memory layout ]]
// --------------------------------------------
// SIZE  DATA
// ----  -------------------------------
//       [memory address grows down ...]
//   0x4 [in: fp]
//   0x4 [in: lr]
// 0x148 [in+out: struct cpu_context]
//   0x4 [sh_switch_t.flags_union]
//   0x4 [out: next_hop]
// ----- -------------------------------

// [[ previous ]]
// --------------------------------------------
// ==> exit @target_address (size: 4)
// *** instruction sets: arm / thumb
// b shadow_exit
//
// ==> shadow_exit @ELF_gap (size: 8)
// *** instruction sets: arm / thumb
// ldr pc, [pc, #-4] *_or_* ldr.w pc, [pc]
// ADDRESS_32(glue_launcher)
//
// ==> glue_launcher @mmap buffer (size: 20)
// *** instruction sets: arm
// str  IP_0, [sp, #-4]  // save IP_0 !!!
// ldr  IP_0, [pc, #4]
// ldr  pc, [pc, #-4]
// ADDRESS_32(shadowhook_interceptor_glue)
// ADDRESS_32(context-pointer)

// [[ the interceptor glue ]]
// --------------------------------------------
// ==> shadowhook_interceptor_glue @.text
// *** instruction sets: arm
// parameter:
// (1) IP_0        : context-pointer
// (2) [sp, #-0x4] : IP_0

.macro m_prolog
  sub   sp, sp, #0x8

  // temporary save context-pointer to stack
  str   IP_0, [sp, #-0x154]

  // save cpsr
  mrs   IP_0, cpsr
  str   IP_0, [sp, #-0x108]
.endm // m_prolog

.macro m_invoke_interceptor label
  add   IP_0, sp, #0x10c   // get sp
  sub   sp, sp, #0x8       // skip cpsr, pc(r15)
  push  {IP_0, lr}         // save sp(r13), lr(r14)
  ldr   IP_0, [sp, #0x118] // restore IP_0 !!!
  push  {r0 - r12}         // save r0 - r12
  push  {fp, lr}           // set fp-chain entry
  mov   fp, sp             // upgrade fp for fp-chain
  .cfi_def_cfa_offset 0x158
  .cfi_rel_offset fp, 0x0
  .cfi_rel_offset lr, 0x4

  ldr   r0, [sp, #-0x4]  // get context-pointer from stack
  add   r1, sp, #0x8     // CPU context
  add   r2, sp, #0x154   // next_hop
  blx   shadowhook_interceptor_caller

  add   sp, sp, #0x8        // skip fp-chain entry
  pop   {r0 - r12}          // restore r0 - r12
  ldr   lr, [sp, #0x4]      // restore lr(r14)
  add   sp, sp, #0x10       // skip sp(r13), lr(r14), pc(r15), cpsr
  str   IP_0, [sp, #-0x50]  // temporary save IP_0 to stack
.endm // m_invoke_interceptor

.macro m_epilog label
  ldr   IP_0, [sp, #-0x108]  // get cpsr
  msr   cpsr, IP_0           // restore cpsr
  ldr   IP_0, [sp, #-0x154]
  add   sp, sp, #0x8
  .cfi_def_cfa_offset 0
  .cfi_restore fp
  .cfi_restore lr

  // Always use ip register, because the target address of
  // the subsequent jump may be a proxy function written in C language.
  // swap ip and [sp, #-0x4]
  str  ip, [sp, #-0xc]
  ldr  ip, [sp, #-0x4]
  str  ip, [sp, #-0x8]
  ldr  ip, [sp, #-0xc]
  str  ip, [sp, #-0x4]    // save ip for "is_proc_start == false" !!!
  ldr  ip, [sp, #-0x8]

  // jump to next_hop
  bx   ip
.endm // m_epilog

// ARM without VFP
ENTRY(shadowhook_interceptor_glue)
  m_prolog

  // skip vregs and fpscr
  sub   sp, sp, #0x104

  m_invoke_interceptor

  // skip vregs and fpscr
  add   sp, sp, #0x104

  m_epilog
END(shadowhook_interceptor_glue)

// ARM VFPv3D16
ENTRY(shadowhook_interceptor_glue_vfpv3d16)
  m_prolog

  // Do we need to save fpsimd registers?
  ldr   IP_0, [sp, #-0x154]
  ldr   IP_0, [IP_0] // get sh_switch_t.flags_union
  str   IP_0, [sp]   // save sh_switch_t.flags_union !!!
  tst   IP_0, #1     // test read_vregs
  bne   .L_save_vregs_vfpv3d16
  sub   sp, sp, #0x104

.L_save_vregs_vfpv3d16_continue:
  m_invoke_interceptor

  // Do we need to restore fpsimd registers?
  ldr   IP_0, [sp, #0x104]  // get sh_switch_t.flags_union !!!
  tst   IP_0, #2            // test write_vregs bit
  bne   .L_restore_vregs_vfpv3d16
  add   sp, sp, #0x104

.L_restore_vregs_vfpv3d16_continue:
  m_epilog

.L_save_vregs_vfpv3d16:
  // save d0 - d15, fpscr
  sub   sp, sp, #0x80
  vpush {d0 - d15}
  vmrs  IP_0, fpscr
  push  {IP_0}
  b     .L_save_vregs_vfpv3d16_continue

.L_restore_vregs_vfpv3d16:
  // restore fpscr, d0 - d15
  pop   {IP_0}
  vmsr  fpscr, IP_0
  vpop  {d0 - d15}
  add   sp, sp, #0x80
  b     .L_restore_vregs_vfpv3d16_continue
END(shadowhook_interceptor_glue_vfpv3d16)

// ARM VFPv3D32
ENTRY(shadowhook_interceptor_glue_vfpv3d32)
  m_prolog

  // Do we need to save fpsimd registers?
  ldr   IP_0, [sp, #-0x154]
  ldr   IP_0, [IP_0] // get sh_switch_t.flags_union
  str   IP_0, [sp]   // save sh_switch_t.flags_union !!!
  tst   IP_0, #1     // test read_vregs
  bne   .L_save_vregs_vfpv3d32
  sub   sp, sp, #0x104

.L_save_vregs_vfpv3d32_continue:
  m_invoke_interceptor

  // Do we need to restore fpsimd registers?
  ldr   IP_0, [sp, #0x104]  // get sh_switch_t.flags_union !!!
  tst   IP_0, #2            // test write_vregs bit
  bne   .L_restore_vregs_vfpv3d32
  add   sp, sp, #0x104

.L_restore_vregs_vfpv3d32_continue:
  m_epilog

.L_save_vregs_vfpv3d32:
  // save d0 - d31, fpscr
  vpush {d16 - d31}
  vpush {d0 - d15}
  vmrs  IP_0, fpscr
  push  {IP_0}
  b     .L_save_vregs_vfpv3d32_continue

.L_restore_vregs_vfpv3d32:
  // restore fpscr, d0 - d31
  pop   {IP_0}
  vmsr  fpscr, IP_0
  vpop  {d0 - d15}
  vpop  {d16 - d31}
  b     .L_restore_vregs_vfpv3d32_continue
END(shadowhook_interceptor_glue_vfpv3d32)

// [[ next ]]
// --------------------------------------------
// CASE (1)
// --------------------------------------------
// is_proc_start == true
// next_hop == enter
//
// ==> enter @mmap buffer
// *** instruction sets: arm / thumb
// [rewritten instructions]
// ldr pc, [pc, #-4] *_or_* ldr.w pc, [pc]
// ADDRESS_32(resume_addr(target_addr + backup_len))
//
// CASE (2)
// --------------------------------------------
// is_proc_start == true
// next_hop == proxy_function
//
// ==> proxy_function @.text
// *** instruction sets: arm / thumb
// ...
// blx enter
// ...
// ret (return to the caller of the hooked function)
//
// ==> enter @mmap buffer
// *** instruction sets: arm / thumb
// [rewritten instructions]
// ldr pc, [pc, #-4] *_or_* ldr.w pc, [pc]
// ADDRESS_32(resume_addr(target_addr + backup_len))
//
// CASE (3)
// --------------------------------------------
// is_proc_start == false
// next_hop == enter
//
// ==> enter @mmap buffer
// *** instruction sets: arm / thumb
// ldr ip, [sp, #-0x4]  // restore ip !!!
// [rewritten instructions]
// ldr pc, [pc, #-4] *_or_* ldr.w pc, [pc]
// ADDRESS_32(resume_addr(target_addr + backup_len))
