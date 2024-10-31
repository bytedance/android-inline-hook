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

// Created by Pengying Xu (xupengying@bytedance.com) on 2021-04-11.

#include "asm.h"

ENTRY_GLOBAL_THUMB(test_t16_helper_global)
    add      r0, r0, r1
    bx       lr
END(test_t16_helper_global)

ENTRY_HIDDEN_THUMB(test_t16_helper_hidden)
    add      r0, r0, r1
    bx       lr
END(test_t16_helper_hidden)


// B T1
ENTRY_GLOBAL_THUMB(test_t16_b_t1)
    cmp      r0, r0
    beq      test_t16_helper_hidden
    nop
    nop
    nop
    bx       lr
END(test_t16_b_t1)

// B T1 fixaddr
ENTRY_GLOBAL_THUMB(test_t16_b_t1_fixaddr)
    cmp      r0, r0
    beq      .t16_b_t1_fixaddr_short_jump
.t16_b_t1_fixaddr_short_jump:
    b        test_t16_helper_hidden
    nop
    nop
END(test_t16_b_t1_fixaddr)

// B T2
ENTRY_GLOBAL_THUMB(test_t16_b_t2)
    b        test_t16_helper_hidden
    nop
    nop
    nop
    nop
END(test_t16_b_t2)

// B T2 fixaddr
ENTRY_GLOBAL_THUMB(test_t16_b_t2_fixaddr)
    b        .t16_b_t2_fixaddr_short_jump
.t16_b_t2_fixaddr_short_jump:
    b        test_t16_helper_hidden
    nop
    nop
    nop
END(test_t16_b_t2_fixaddr)

// BX T1
ENTRY_GLOBAL_THUMB_BOUND(test_t16_bx_t1)
    push     {lr}
    nop
    bx       pc // thumb -> arm
    nop
.code 32
    blx      test_t16_helper_global // arm -> thumb
    pop      {pc}
END(test_t16_bx_t1)

// ADD REG T2
ENTRY_GLOBAL_THUMB(test_t16_add_reg_t2)
    push     {r3, r4}
    movs     r3, #0
    movs     r4, #0
    add      r3, pc
    add      r4, pc
    nop
    add.w    r3, r3, #2
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_t16_helper_global
    bx       lr
END(test_t16_add_reg_t2)

// MOV REG T1
ENTRY_GLOBAL_THUMB(test_t16_mov_reg_t1)
    push     {r8, r9}
    mov      r8, pc
    nop
    mov      r9, pc
    nop
    add.w    r8, r8, #4
    cmp      r8, r9
    nop
    pop      {r8, r9}
    beq      test_t16_helper_global
    bx       lr
END(test_t16_mov_reg_t1)

// ADR T1
ENTRY_GLOBAL_THUMB(test_t16_adr_t1)
    push     {r4, lr}
    adr      r4, test_t16_helper_hidden_tail
    nop
    nop
    nop
    adr      lr, .t16_adr_t1_ret
    add      lr, lr, #1
    mov      pc, r4
.t16_adr_t1_ret:
    pop      {r4, pc}
END(test_t16_adr_t1)

// LDR LIT T1
ENTRY_GLOBAL_THUMB(test_t16_ldr_lit_t1)
    push     {r3, r4}
    ldr.w    r3, .t16_ldr_lit_t1_long1
    nop
    nop
    ldr.w    r4, .t16_ldr_lit_t1_long2
    cmp      r3, r4
    pop      {r3, r4}
    beq      test_t16_helper_global
    bx       lr
.t16_ldr_lit_t1_long1:
    .long    12345
.t16_ldr_lit_t1_long2:
    .long    12345
END(test_t16_ldr_lit_t1)

// CBZ T1
ENTRY_GLOBAL_THUMB(test_t16_cbz_t1)
    push     {r0}
    movs     r0, #0
    cbz      r0, .t16_cbz_t1_next
    pop      {r0}
    bx       lr
.t16_cbz_t1_next:
    pop      {r0}
    b        test_t16_helper_global
END(test_t16_cbz_t1)

// CBZ T1 fixaddr
ENTRY_GLOBAL_THUMB(test_t16_cbz_t1_fixaddr)
    cbz      r0, .t16_cbz_t1_fixaddr_next // r0 == 4
    b        test_t16_helper_global
.t16_cbz_t1_fixaddr_next:
    bx       lr
    nop
    nop
END(test_t16_cbz_t1_fixaddr)

// CBNZ T1
ENTRY_GLOBAL_THUMB(test_t16_cbnz_t1)
    push     {r0}
    movs     r0, #1
    cbnz     r0, .t16_cbnz_t1_next
    pop      {r0}
    bx       lr
.t16_cbnz_t1_next:
    pop      {r0}
    b        test_t16_helper_global
END(test_t16_cbnz_t1)

// CBNZ T1 fixaddr
ENTRY_GLOBAL_THUMB(test_t16_cbnz_t1_fixaddr)
    cbnz     r0, .t16_cbnz_t1_fixaddr_next // r0 == 4
    bx       lr
.t16_cbnz_t1_fixaddr_next:
    b        test_t16_helper_global
    nop
    nop
END(test_t16_cbnz_t1_fixaddr)

// IT T1 case1
ENTRY_GLOBAL_THUMB(test_t16_it_t1_case1)
    cmp      r0, r0
    iteet    eq
    addeq.w  r0, r0, #1
    addne    r0, r0, #11
    subne.w  r0, r0, #10
    subeq    r0, r0, #1
    b        test_t16_helper_global
END(test_t16_it_t1_case1)

// IT T1 case2
ENTRY_GLOBAL_THUMB(test_t16_it_t1_case2)
    cmp      r0, r0
    itt      eq
    addeq    r0, r0, #0
    beq      test_t16_helper_global
    bx       lr
END(test_t16_it_t1_case2)

ENTRY_GLOBAL_THUMB(test_t16_it_t1_case3)
  cmp      r0, r0         // Compare r0 with 0
  itete    ne             // If-Then-Else-Then-Else block for Not Equal
  addne    r1, r1, #1     // If r0 != 0, add 1 to r1
  addeq    r2, r2, #2     // If r0 == 0, add 2 to r2
  subne    r3, r3, #3     // If r0 != 0, subtract 3 from r3
  moveq    r4, #4         // If r0 == 0, move 4 to r4
  b        test_t16_helper_global
END(test_t16_it_t1_case3)


ENTRY_HIDDEN_THUMB(test_t16_helper_hidden_tail)
    add      r0, r0, r1
    bx       lr
END(test_t16_helper_hidden_tail)
