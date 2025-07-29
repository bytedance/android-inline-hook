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

#include "asm.h"

ENTRY_GLOBAL_THUMB(test_t32_helper_global)
    add      r0, r0, r1
    bx       lr
END(test_t32_helper_global)

ENTRY_HIDDEN_THUMB(test_t32_helper_hidden)
    add      r0, r0, r1
    bx       lr
END(test_t32_helper_hidden)


// B T3
ENTRY_GLOBAL_THUMB(test_t32_b_t3)
    cmp      r0, r0
    beq.w    test_t32_helper_global
    nop
    nop
    nop
    bx       lr
END(test_t32_b_t3)

// B T4
ENTRY_GLOBAL_THUMB(test_t32_b_t4)
    b.w      test_t32_helper_global
    nop
    nop
    nop
    nop
END(test_t32_b_t4)

// B T4 fixaddr
ENTRY_GLOBAL_THUMB(test_t32_b_t4_fixaddr)
    b.w      .L_t32_b_t4_fixaddr_next
    bx       lr
.L_t32_b_t4_fixaddr_next:
    b.w      test_t32_helper_global
    bx       lr
END(test_t32_b_t4_fixaddr)

// BL IMM T1
ENTRY_GLOBAL_THUMB(test_t32_bl_imm_t1)
    push     {lr}
    bl       test_t32_helper_hidden
    nop
    nop
    pop      {pc}
END(test_t32_bl_imm_t1)

// BLX IMM T2
ENTRY_GLOBAL_THUMB(test_t32_blx_imm_t2)
    push     {lr}
    blx      test_t32_helper_global
    nop
    nop
    pop      {pc}
END(test_t32_blx_imm_t2)

// ADR T2
ENTRY_GLOBAL_THUMB(test_t32_adr_t2)
    push     {r8, lr}
    adr.w    r8, test_t32_helper_hidden
    nop
    nop
    nop
    adr      lr, .L_t32_adr_t2_ret
    add      lr, lr, #1
    mov      pc, r8
.L_t32_adr_t2_ret:
    pop      {r8, pc}
END(test_t32_adr_t2)

// ADR T3
ENTRY_GLOBAL_THUMB(test_t32_adr_t3)
    push     {r8, lr}
    adr.w    r8, test_t32_helper_hidden_tail
    nop
    nop
    nop
    adr      lr, .L_t32_adr_t3_ret
    add      lr, lr, #1
    mov      pc, r8
.L_t32_adr_t3_ret:
    pop      {r8, pc}
END(test_t32_adr_t3)

// LDR LIT T2 case1
ENTRY_GLOBAL_THUMB(test_t32_ldr_lit_t2_case1)
    push     {r8, r9}
    ldr.w    r8, .long1
    ldr.w    r9, .long2
    cmp      r8, r9
    pop      {r8, r9}
    beq.w    test_t32_helper_global
    bx       lr
.long1:
    .long    12345
.long2:
    .long    12345
END(test_t32_ldr_lit_t2_case1)

// LDR LIT T2 case2
ENTRY_GLOBAL_THUMB_BOUND(test_t32_ldr_lit_t2_case2)
    ldr.w    pc, .L_test_t32_helper_global_ptr
    nop
    nop
.L_test_t32_helper_global_ptr:
    .long    0
    bx       lr
END(test_t32_ldr_lit_t2_case2)

// PLD LIT T1
ENTRY_GLOBAL_THUMB(test_t32_pld_lit_t1)
    pld      test_t32_helper_global
    nop
    nop
    nop
    b.w      test_t32_helper_global
END(test_t32_pld_lit_t1)

// PLI LIT T3
ENTRY_GLOBAL_THUMB(test_t32_pli_lit_t3)
    pli      test_t32_helper_global
    nop
    nop
    nop
    b.w      test_t32_helper_global
END(test_t32_pli_lit_t3)

// TBB T1
ENTRY_GLOBAL_THUMB_BOUND(test_t32_tbb_t1)
    push     {r0}
    movs     r0, #1
    tbb      [pc, r0]
.L_tbb_branchtable:
    .byte    ((.L_tbb_case0 - .L_tbb_branchtable) / 2)
    .byte    ((.L_tbb_case1 - .L_tbb_branchtable) / 2)
    .byte    ((.L_tbb_case2 - .L_tbb_branchtable) / 2)
    .byte    ((.L_tbb_case3 - .L_tbb_branchtable) / 2)
.L_tbb_case0:
    pop      {r0}
    bx       lr
.L_tbb_case1:
    pop      {r0}
    b        test_t32_helper_global
.L_tbb_case2:
    pop      {r0}
    bx       lr
.L_tbb_case3:
    pop      {r0}
    bx       lr
END(test_t32_tbb_t1)

// TBH T1
ENTRY_GLOBAL_THUMB_BOUND(test_t32_tbh_t1)
    push     {r0}
    movs     r0, #1
    tbh      [pc, r0, lsl #1]
.L_tbh_branchtable:
    .short   ((.L_tbh_case0 - .L_tbh_branchtable) / 2)
    .short   ((.L_tbh_case1 - .L_tbh_branchtable) / 2)
    .short   ((.L_tbh_case2 - .L_tbh_branchtable) / 2)
    .short   ((.L_tbh_case3 - .L_tbh_branchtable) / 2)
.L_tbh_case0:
    pop      {r0}
    bx       lr
.L_tbh_case1:
    pop      {r0}
    b        test_t32_helper_global
.L_tbh_case2:
    pop      {r0}
    bx       lr
.L_tbh_case3:
    pop      {r0}
    bx       lr
END(test_t32_tbh_t1)

// VLDR LIT T1 case1
ENTRY_GLOBAL_THUMB_BOUND(test_t32_vldr_lit_t1_case1)
    vpush    {s1, s2}
    vldr     s1, .float1
    vldr     s2, .float2
    vcmp.f32 s1, s2
    vmrs     APSR_nzcv, FPSCR
    vpop     {s1, s2}
    beq.w    test_t32_helper_global
    bx       lr
    nop
.float1:
    .float   123.45
.float2:
    .float   123.45
END(test_t32_vldr_lit_t1_case1)

// VLDR LIT T1 case2
ENTRY_GLOBAL_THUMB_BOUND(test_t32_vldr_lit_t1_case2)
    vpush    {d1, d2}
    vldr     d1, .double1
    vldr     d2, .double2
    vcmp.f64 d1, d2
    vmrs     APSR_nzcv, FPSCR
    vpop     {d1, d2}
    beq.w    test_t32_helper_global
    bx       lr
    nop
.double1:
    .double  123.45
.double2:
    .double  123.45
END(test_t32_vldr_lit_t1_case2)


ENTRY_HIDDEN_THUMB(test_t32_helper_hidden_tail)
    add      r0, r0, r1
    bx       lr
END(test_t32_helper_hidden_tail)


ENTRY_GLOBAL_THUMB(test_t32_instr)
    nop.w
    nop.w
    add.w      r0, r0, r1
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    nop.w
    bx       lr
END(test_t32_instr)
