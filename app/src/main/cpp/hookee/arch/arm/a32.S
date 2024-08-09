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

#include "asm.h"

ENTRY_GLOBAL_ARM(test_a32_helper_global)
    add      r0, r0, r1
    bx       lr
END(test_a32_helper_global)

// B A1, hidden function
ENTRY_HIDDEN_ARM(test_hidden_func)
    cmp      r0, r1
    bne      test_a32_helper_hidden_tail
    bx       lr
END(test_hidden_func)

ENTRY_HIDDEN_ARM(test_a32_helper_hidden)
    add      r0, r0, r1
    bx       lr
END(test_a32_helper_hidden)

ENTRY_HIDDEN_THUMB(test_a32_helper_hidden_thumb)
    add      r0, r0, r1
    bx       lr
END(test_a32_helper_hidden_thumb)


// B A1
ENTRY_GLOBAL_ARM(test_a32_b_a1)
    cmp      r0, r1
    bne      test_a32_helper_hidden_tail
    bx       lr
END(test_a32_b_a1)

// B A1 fixaddr
ENTRY_GLOBAL_ARM(test_a32_b_a1_fixaddr)
    b        .a32_b_a1_fixaddr_short_jump
.a32_b_a1_fixaddr_short_jump:
    b        test_a32_helper_hidden_tail
    bx       lr
END(test_a32_b_a1_fixaddr)

// BX A1
ENTRY_GLOBAL_ARM(test_a32_bx_a1)
    push     {lr}
    bx       pc
    nop
    bl       test_a32_helper_global
    pop      {pc}
END(test_a32_bx_a1)

// BL IMM A1
ENTRY_GLOBAL_ARM(test_a32_bl_imm_a1)
    push     {lr}
    bl       test_a32_helper_hidden
    nop
    nop
    pop      {pc}
END(test_a32_bl_imm_a1)

// BLX IMM A2
ENTRY_GLOBAL_ARM(test_a32_blx_imm_a2)
    push     {lr}
    blx      test_a32_helper_hidden_thumb
    nop
    nop
    pop      {pc}
END(test_a32_blx_imm_a2)

// ADD REG A1 case1
ENTRY_GLOBAL_ARM(test_a32_add_reg_a1_case1)
    push     {r3, r4}
    add      r3, pc, r0, LSL #1
    add      r4, pc, r0, LSL #1
    add      r3, r3, 4
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_a32_helper_global
    bx       lr
END(test_a32_add_reg_a1_case1)

// ADD REG A1 case2
ENTRY_GLOBAL_ARM(test_a32_add_reg_a1_case2)
    push     {r3, r4}
    add      r3, r0, pc
    add      r4, r0, pc
    add      r3, r3, 4
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_a32_helper_global
    bx       lr
END(test_a32_add_reg_a1_case2)

// ADD REG A1 case3
ENTRY_GLOBAL_ARM(test_a32_add_reg_a1_case3)
    cmp      r0, r1
    addne    pc, pc, r0 // r0 == 4
    bx       lr
    bx       lr
    b        test_a32_helper_global
    bx       lr
END(test_a32_add_reg_a1_case3)

// SUB REG A1 case1
ENTRY_GLOBAL_ARM(test_a32_sub_reg_a1_case1)
    push     {r3, r4}
    sub      r3, pc, r0, LSL #1
    sub      r4, pc, r0, LSL #1
    add      r3, r3, 4
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_a32_helper_global
    bx       lr
END(test_a32_sub_reg_a1_case1)

// SUB REG A1 case2
ENTRY_GLOBAL_ARM(test_a32_sub_reg_a1_case2)
    push     {r3, r4}
    sub      r3, r0, pc
    sub      r4, r0, pc
    sub      r3, r3, 4
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_a32_helper_global
    bx       lr
END(test_a32_sub_reg_a1_case2)

// SUB REG A1 case3
ENTRY_GLOBAL_ARM(test_a32_sub_reg_a1_case3)
    cmp      r0, r1
    subne    pc, pc, r0 // r0 == 4
    b        test_a32_helper_global
    bx       lr
END(test_a32_sub_reg_a1_case3)

// ADR A1 case1
ENTRY_GLOBAL_ARM(test_a32_adr_a1_case1)
    push     {r8, lr}
    adr      r8, test_a32_helper_hidden_tail
    adr      lr, #0
    mov      pc, r8
    pop      {r8, pc}
END(test_a32_adr_a1_case1)

// ADR A1 case2
ENTRY_GLOBAL_ARM(test_a32_adr_a1_case2)
    cmp      r0, r1
    adrne    pc, test_a32_helper_hidden_tail
    bx       lr
END(test_a32_adr_a1_case2)

// ADR A2 case1
ENTRY_GLOBAL_ARM(test_a32_adr_a2_case1)
    push     {r8, lr}
    adr      r8, test_a32_helper_hidden
    adr      lr, #0
    mov      pc, r8
    pop      {r8, pc}
END(test_a32_adr_a2_case1)

// ADR A2 case2
ENTRY_GLOBAL_ARM(test_a32_adr_a2_case2)
    cmp      r0, r1
    adrne    pc, test_a32_helper_hidden
    bx       lr
END(test_a32_adr_a2_case2)

// MOV REG A1 case1
ENTRY_GLOBAL_ARM(test_a32_mov_reg_a1_case1)
    push     {r3, r4}
    mov      r3, pc, LSL #1
    mov      r4, pc, LSL #1
    add      r3, r3, 8
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_a32_helper_global
    bx       lr
END(test_a32_mov_reg_a1_case1)

// MOV REG A1 case2
ENTRY_GLOBAL_ARM(test_a32_mov_reg_a1_case2)
    push     {r3, r4}
    movs     r3, pc
    mov      r4, pc
    addne    r3, r3, 4
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_a32_helper_global
    bx       lr
END(test_a32_mov_reg_a1_case2)

// MOV REG A1 case3
ENTRY_GLOBAL_ARM(test_a32_mov_reg_a1_case3)
    cmp      r0, r1
    movne    pc, pc
    bx       lr
    b        test_a32_helper_global
    bx       lr
END(test_a32_mov_reg_a1_case3)

// LDR LIT A1 case1
ENTRY_GLOBAL_ARM(test_a32_ldr_lit_a1_case1)
    push     {r8, r9}
    ldr      r8, .long1
    ldr      r9, .long2
    cmp      r8, r9
    pop      {r8, r9}
    beq      test_t32_helper_global
    bx       lr
.long1:
    .long    12345
.long2:
    .long    12345
END(test_a32_ldr_lit_a1_case1)

// LDR LIT A1 case2
ENTRY_GLOBAL_ARM_BOUND(test_a32_ldr_lit_a1_case2)
    cmp      r0, r1
    ldrne    pc, .a32_ldr_lit_a1_case2_data
    bx       lr
.a32_ldr_lit_a1_case2_data:
    .long    0
    bx       lr
END(test_a32_ldr_lit_a1_case2)

// LDR REG A1 case1
ENTRY_GLOBAL_ARM(test_a32_ldr_reg_a1_case1)
    push     {r8, r9}
    ldr      r8, [pc, r0, LSL #2] // r0 == 4
    ldr      r9, [pc, r0, LSL #2] // r0 == 4
    cmp      r8, r9
    pop      {r8, r9}
    beq      test_t32_helper_global
    bx       lr
    .long    12345
    .long    12345
END(test_a32_ldr_reg_a1_case1)

// LDR REG A1 case2
ENTRY_GLOBAL_ARM_BOUND(test_a32_ldr_reg_a1_case2)
    cmp      r0, r1
    ldrne    pc, [pc, r0, LSL #1] // r0 == 4
    bx       lr
    nop
    nop
    .long    0
    bx       lr
END(test_a32_ldr_reg_a1_case2)


ENTRY_HIDDEN_ARM(test_a32_helper_hidden_tail)
    add      r0, r0, r1
    bx       lr
END(test_a32_helper_hidden_tail)
