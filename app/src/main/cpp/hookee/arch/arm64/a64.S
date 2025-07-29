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

ENTRY_GLOBAL_ARM(test_a64_helper_global)
    add      x0, x0, x1
    ret
END(test_a64_helper_global)


// B, hidden function
ENTRY_HIDDEN_ARM(test_hidden_func)
    b        test_a64_helper_global
    nop
    nop
    nop
    nop
    ret
END(test_hidden_func)


// benchmark for unique mode
ENTRY_GLOBAL_ARM(test_a64_for_unique)
    add      x0, x0, x1
    nop
    nop
    nop
    nop
    ret
END(test_a64_for_unique)

// benchmark for multi mode
ENTRY_GLOBAL_ARM(test_a64_for_multi)
    add      x0, x0, x1
    nop
    nop
    nop
    nop
    ret
END(test_a64_for_multi)

// benchmark for shared mode
ENTRY_GLOBAL_ARM(test_a64_for_shared)
    add      x0, x0, x1
    nop
    nop
    nop
    nop
    ret
END(test_a64_for_shared)


// B
ENTRY_GLOBAL_ARM(test_a64_b)
    b        test_a64_helper_global
    nop
    nop
    nop
    nop
    ret
END(test_a64_b)

// B fixaddr
ENTRY_GLOBAL_ARM(test_a64_b_fixaddr)
    b        .L_a64_b_fixaddr_next
    ret
.L_a64_b_fixaddr_next:
    b        test_a64_helper_global
    nop
    nop
    ret
END(test_a64_b_fixaddr)

// B cond
ENTRY_GLOBAL_ARM(test_a64_b_cond)
    cmp      x0, x1
    b.ne     test_a64_helper_hidden_tail
    nop
    nop
    nop
    ret
END(test_a64_b_cond)

// B cond fixaddr
ENTRY_GLOBAL_ARM(test_a64_b_cond_fixaddr)
    cmp      x0, x1
    b.ne     .L_a64_b_cond_fixaddr_next
    ret
.L_a64_b_cond_fixaddr_next:
    b        test_a64_helper_hidden_tail
    nop
    ret
END(test_a64_b_cond_fixaddr)

// BL
ENTRY_GLOBAL_ARM(test_a64_bl)
    stp      x29, x30, [sp, #-0x10]!
    bl       test_a64_helper_global
    nop
    nop
    ldp      x29, x30, [sp], #0x10
    ret
END(test_a64_bl)

// BL fixaddr
ENTRY_GLOBAL_ARM(test_a64_bl_fixaddr)
    stp      x29, x30, [sp, #-0x10]!
    bl       .L_a64_bl_fixaddr_next
    b        .L_a64_bl_fixaddr_ret
.L_a64_bl_fixaddr_next:
    add      x0, x0, x1
    ret
.L_a64_bl_fixaddr_ret:
    ldp      x29, x30, [sp], #0x10
    ret
END(test_a64_bl_fixaddr)

// ADR
ENTRY_GLOBAL_ARM(test_a64_adr)
    adr      x9, test_a64_helper_hidden_tail
    nop
    nop
    nop
    nop
    br       x9
END(test_a64_adr)

// ADRP
ENTRY_GLOBAL_ARM(test_a64_adrp)
    adrp     x9, 0x1000
    nop
    nop
    nop
    adrp     x10, 0x1000
    cmp      x9, x10
    b.eq     test_a64_helper_global
    ret
END(test_a64_adrp)

// LDR LIT 32
ENTRY_GLOBAL_ARM(test_a64_ldr_lit_32)
    stp      x8, x9, [sp, #-0x10]!
    ldr      w8, .L_a64_ldr_lit_32_1
    ldr      w9, .L_a64_ldr_lit_32_2
    cmp      w8, w9
    ldp      x8, x9, [sp], #0x10
    b.eq     test_a64_helper_global
    ret
.L_a64_ldr_lit_32_1:
    .word    12345
.L_a64_ldr_lit_32_2:
    .word    12345
END(test_a64_ldr_lit_32)

// LDR LIT 64
ENTRY_GLOBAL_ARM(test_a64_ldr_lit_64)
    stp      x8, x9, [sp, #-0x10]!
    ldr      x8, .L_a64_ldr_lit_64_1
    ldr      x9, .L_a64_ldr_lit_64_2
    cmp      x8, x9
    ldp      x8, x9, [sp], #0x10
    b.eq     test_a64_helper_global
    ret
.L_a64_ldr_lit_64_1:
    .dword   0x123456789ABCDEF
.L_a64_ldr_lit_64_2:
    .dword   0x123456789ABCDEF
END(test_a64_ldr_lit_64)

// LDRSW LIT
ENTRY_GLOBAL_ARM(test_a64_ldrsw_lit)
    stp      x8, x9, [sp, #-0x10]!
    ldrsw    x8, .L_a64_ldrsw_lit_1
    ldrsw    x9, .L_a64_ldrsw_lit_2
    cmp      x8, x9
    ldp      x8, x9, [sp], #0x10
    b.eq     test_a64_helper_global
    ret
.L_a64_ldrsw_lit_1:
    .dword   0x123456789ABCDEF
.L_a64_ldrsw_lit_2:
    .dword   0x123456789ABCDEF
END(test_a64_ldrsw_lit)

// PRFM LIT
ENTRY_GLOBAL_ARM(test_a64_prfm_lit)
    prfm     pldl1keep, test_a64_helper_hidden_tail
    nop
    nop
    nop
    nop
    b        test_a64_helper_global
END(test_a64_prfm_lit)

// LDR SIMD LIT 32
ENTRY_GLOBAL_ARM(test_a64_ldr_simd_lit_32)
    stp      q8, q9, [sp, #-0x20]!
    ldr      s8, .L_a64_ldr_simd_lit_32_1
    ldr      s9, .L_a64_ldr_simd_lit_32_2
    fcmp     s8, s9
    ldp      q8, q9, [sp], #0x20
    b.eq     test_a64_helper_global
    ret
.L_a64_ldr_simd_lit_32_1:
    .float   123.45
.L_a64_ldr_simd_lit_32_2:
    .float   123.45
END(test_a64_ldr_simd_lit_32)

// LDR SIMD LIT 64
ENTRY_GLOBAL_ARM(test_a64_ldr_simd_lit_64)
    stp      q8, q9, [sp, #-0x20]!
    ldr      d8, .L_a64_ldr_simd_lit_64_1
    ldr      d9, .L_a64_ldr_simd_lit_64_2
    fcmp     d8, d9
    ldp      q8, q9, [sp], #0x20
    b.eq     test_a64_helper_global
    ret
.L_a64_ldr_simd_lit_64_1:
    .double  123.45
.L_a64_ldr_simd_lit_64_2:
    .double  123.45
END(test_a64_ldr_simd_lit_64)

// LDR SIMD LIT 128
ENTRY_GLOBAL_ARM(test_a64_ldr_simd_lit_128)
    stp      q8, q9, [sp, #-0x20]!
    stp      x0, x1, [sp, #-0x10]!
    ldr      q8, .L_a64_ldr_simd_lit_128_1
    ldr      q9, .L_a64_ldr_simd_lit_128_2
    mov      x0, v8.d[0]
    mov      x1, v9.d[0]
    cmp      x0, x1
    b.ne     .L_a64_ldr_simd_lit_128_ret
    mov      x0, v8.d[1]
    mov      x1, v9.d[1]
    cmp      x0, x1
    b.ne     .L_a64_ldr_simd_lit_128_ret
    ldp      x0, x1, [sp], #0x10
    ldp      q8, q9, [sp], #0x20
    b        test_a64_helper_global
    ret
.L_a64_ldr_simd_lit_128_ret:
    ldp      x0, x1, [sp], #0x10
    ldp      q8, q9, [sp], #0x20
    ret
.L_a64_ldr_simd_lit_128_1:
    .dword   0x123456789ABCDEF
    .dword   0x123456789ABCDEF
.L_a64_ldr_simd_lit_128_2:
    .dword   0x123456789ABCDEF
    .dword   0x123456789ABCDEF
END(test_a64_ldr_simd_lit_128)

// CBZ
ENTRY_GLOBAL_ARM(test_a64_cbz)
    mov      x9, #0
    cbz      x9, .L_a64_cbz_next
    nop
    nop
    nop
    ret
.L_a64_cbz_next:
    b        test_a64_helper_global
END(test_a64_cbz)

// CBZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_cbz_fixaddr)
    mov      x9, #0
    cbz      x9, .L_a64_cbz_fixaddr_next
    ret
.L_a64_cbz_fixaddr_next:
    b        test_a64_helper_global
    nop
    nop
END(test_a64_cbz_fixaddr)

// CBNZ
ENTRY_GLOBAL_ARM(test_a64_cbnz)
    mov      w16, #1
    cbnz     w16, .L_a64_cbnz_next
    nop
    nop
    nop
    ret
.L_a64_cbnz_next:
    b        test_a64_helper_global
END(test_a64_cbnz)

// CBNZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_cbnz_fixaddr)
    mov      w16, #1
    cbnz     w16, .L_a64_cbnz_fixaddr_next
    ret
.L_a64_cbnz_fixaddr_next:
    b        test_a64_helper_global
    nop
    nop
END(test_a64_cbnz_fixaddr)

// TBZ
ENTRY_GLOBAL_ARM(test_a64_tbz)
    mov      x9, #0b1000
    tbz      x9, #2, .L_a64_tbz_next
    nop
    nop
    nop
    ret
.L_a64_tbz_next:
    b        test_a64_helper_global
END(test_a64_tbz)

// TBZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_tbz_fixaddr)
    mov      x9, #0b1000
    tbz      x9, #2, .L_a64_tbz_fixaddr_next
    ret
.L_a64_tbz_fixaddr_next:
    b        test_a64_helper_global
    nop
    nop
END(test_a64_tbz_fixaddr)

// TBNZ
ENTRY_GLOBAL_ARM(test_a64_tbnz)
    mov      w16, #0b1100
    tbnz     w16, #2, .L_a64_tbnz_next
    nop
    nop
    nop
    ret
.L_a64_tbnz_next:
    b        test_a64_helper_global
END(test_a64_tbnz)

// TBNZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_tbnz_fixaddr)
    mov      w16, #0b1100
    tbnz     w16, #2, .L_a64_tbnz_fixaddr_next
    ret
.L_a64_tbnz_fixaddr_next:
    b        test_a64_helper_global
    nop
    nop
END(test_a64_tbnz_fixaddr)

// instr B
ENTRY_GLOBAL_ARM(test_a64_instr_b)
    nop
    mov      x17, #123
    b        #8
    mov      x17, #345
    cmp      x17, #123
    nop
    nop
    b.ne     #8
    add      x0, x0, x1
    nop
    nop
    nop
    nop
    ret
END(test_a64_instr_b)

// instr B cond
ENTRY_GLOBAL_ARM(test_a64_instr_b_cond)
    mov      x17, #123
    cmp      x17, #123
    b.eq     #8
    mov      x17, #345
    cmp      x17, #123
    nop
    nop
    b.ne     #8
    add      x0, x0, x1
    nop
    nop
    nop
    nop
    ret
END(test_a64_instr_b_cond)

// instr CBZ
ENTRY_GLOBAL_ARM(test_a64_instr_cbz)
    nop
    mov      x17, #0
    cbz      x17, .L_a64_instr_cbz_next
    nop
    nop
    ret
.L_a64_instr_cbz_next:
    cmp      x17, #0
    b.eq     test_a64_helper_global
    ret
END(test_a64_instr_cbz)

// instr TBZ
ENTRY_GLOBAL_ARM(test_a64_instr_tbz)
    nop
    mov      x17, #0b1000
    tbz      x17, #2, .L_a64_instr_tbz_next
    nop
    nop
    ret
.L_a64_instr_tbz_next:
    cmp      x17, #0b1000
    b        test_a64_helper_global
    ret
END(test_a64_instr_tbz)

ENTRY_HIDDEN_ARM(test_a64_helper_hidden_tail)
    add      x0, x0, x1
    ret
END(test_a64_helper_global_tail)
