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
    ret
END(test_hidden_func)


// B
ENTRY_GLOBAL_ARM(test_a64_b)
    b        test_a64_helper_global
    nop
    nop
    nop
    ret
END(test_a64_b)

// B fixaddr
ENTRY_GLOBAL_ARM(test_a64_b_fixaddr)
    b        .a64_b_fixaddr_short_jump
    ret
.a64_b_fixaddr_short_jump:
    b        test_a64_helper_global
    nop
    ret
END(test_a64_b_fixaddr)

// B cond
ENTRY_GLOBAL_ARM(test_a64_b_cond)
    cmp      x0, x1
    b.ne     test_a64_helper_hidden_tail
    nop
    nop
    ret
END(test_a64_b_cond)

// B cond fixaddr
ENTRY_GLOBAL_ARM(test_a64_b_cond_fixaddr)
    cmp      x0, x1
    b.ne     .a64_b_fixaddr_cond_short_jump
    ret
.a64_b_fixaddr_cond_short_jump:
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
    bl       .a64_bl_fixaddr_short_jump
    b        .a64_bl_fixaddr_ret
.a64_bl_fixaddr_short_jump:
    add      x0, x0, x1
    ret
.a64_bl_fixaddr_ret:
    ldp      x29, x30, [sp], #0x10
    ret
END(test_a64_bl_fixaddr)

// ADR
ENTRY_GLOBAL_ARM(test_a64_adr)
    adr      x16, test_a64_helper_hidden_tail
    nop
    nop
    nop
    br       x16
END(test_a64_adr)

// ADRP
ENTRY_GLOBAL_ARM(test_a64_adrp)
    adrp     x16, 0x1000
    nop
    nop
    nop
    adrp     x17, 0x1000
    cmp      x16, x17
    b.eq     test_a64_helper_global
    ret
END(test_a64_adrp)

// LDR LIT 32
ENTRY_GLOBAL_ARM(test_a64_ldr_lit_32)
    stp      x8, x9, [sp, #-0x10]!
    ldr      w8, .a64_ldr_lit_32_1
    ldr      w9, .a64_ldr_lit_32_2
    cmp      w8, w9
    ldp      x8, x9, [sp], #0x10
    b.eq     test_a64_helper_global
    ret
.a64_ldr_lit_32_1:
    .word    12345
.a64_ldr_lit_32_2:
    .word    12345
END(test_a64_ldr_lit_32)

// LDR LIT 64
ENTRY_GLOBAL_ARM(test_a64_ldr_lit_64)
    stp      x8, x9, [sp, #-0x10]!
    ldr      x8, .a64_ldr_lit_64_1
    ldr      x9, .a64_ldr_lit_64_2
    cmp      x8, x9
    ldp      x8, x9, [sp], #0x10
    b.eq     test_a64_helper_global
    ret
.a64_ldr_lit_64_1:
    .dword   0x123456789ABCDEF
.a64_ldr_lit_64_2:
    .dword   0x123456789ABCDEF
END(test_a64_ldr_lit_64)

// LDRSW LIT
ENTRY_GLOBAL_ARM(test_a64_ldrsw_lit)
    stp      x8, x9, [sp, #-0x10]!
    ldrsw    x8, .a64_ldrsw_lit_1
    ldrsw    x9, .a64_ldrsw_lit_2
    cmp      x8, x9
    ldp      x8, x9, [sp], #0x10
    b.eq     test_a64_helper_global
    ret
.a64_ldrsw_lit_1:
    .dword   0x123456789ABCDEF
.a64_ldrsw_lit_2:
    .dword   0x123456789ABCDEF
END(test_a64_ldrsw_lit)

// PRFM LIT
ENTRY_GLOBAL_ARM(test_a64_prfm_lit)
    prfm     pldl1keep, test_a64_helper_hidden_tail
    nop
    nop
    nop
    b        test_a64_helper_global
END(test_a64_prfm_lit)

// LDR SIMD LIT 32
ENTRY_GLOBAL_ARM(test_a64_ldr_simd_lit_32)
    stp      q8, q9, [sp, #-0x20]!
    ldr      s8, .a64_ldr_simd_lit_32_1
    ldr      s9, .a64_ldr_simd_lit_32_2
    fcmp     s8, s9
    ldp      q8, q9, [sp], #0x20
    b.eq     test_a64_helper_global
    ret
.a64_ldr_simd_lit_32_1:
    .float   123.45
.a64_ldr_simd_lit_32_2:
    .float   123.45
END(test_a64_ldr_simd_lit_32)

// LDR SIMD LIT 64
ENTRY_GLOBAL_ARM(test_a64_ldr_simd_lit_64)
    stp      q8, q9, [sp, #-0x20]!
    ldr      d8, .a64_ldr_simd_lit_64_1
    ldr      d9, .a64_ldr_simd_lit_64_2
    fcmp     d8, d9
    ldp      q8, q9, [sp], #0x20
    b.eq     test_a64_helper_global
    ret
.a64_ldr_simd_lit_64_1:
    .double  123.45
.a64_ldr_simd_lit_64_2:
    .double  123.45
END(test_a64_ldr_simd_lit_64)

// LDR SIMD LIT 128
ENTRY_GLOBAL_ARM(test_a64_ldr_simd_lit_128)
    stp      q8, q9, [sp, #-0x20]!
    stp      x0, x1, [sp, #-0x10]!
    ldr      q8, .a64_ldr_simd_lit_128_1
    ldr      q9, .a64_ldr_simd_lit_128_2
    mov      x0, v8.d[0]
    mov      x1, v9.d[0]
    cmp      x0, x1
    b.ne     .a64_ldr_simd_lit_128_ret
    mov      x0, v8.d[1]
    mov      x1, v9.d[1]
    cmp      x0, x1
    b.ne     .a64_ldr_simd_lit_128_ret
    ldp      x0, x1, [sp], #0x10
    ldp      q8, q9, [sp], #0x20
    b        test_a64_helper_global
    ret
.a64_ldr_simd_lit_128_ret:
    ldp      x0, x1, [sp], #0x10
    ldp      q8, q9, [sp], #0x20
    ret
.a64_ldr_simd_lit_128_1:
    .dword   0x123456789ABCDEF
    .dword   0x123456789ABCDEF
.a64_ldr_simd_lit_128_2:
    .dword   0x123456789ABCDEF
    .dword   0x123456789ABCDEF
END(test_a64_ldr_simd_lit_128)

// CBZ
ENTRY_GLOBAL_ARM(test_a64_cbz)
    mov      x16, #0
    cbz      x16, .a64_cbz_next
    nop
    nop
    ret
.a64_cbz_next:
    b        test_a64_helper_global
END(test_a64_cbz)

// CBZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_cbz_fixaddr)
    mov      x16, #0
    cbz      x16, .a64_cbz_fixaddr_next
    ret
.a64_cbz_fixaddr_next:
    b        test_a64_helper_global
END(test_a64_cbz_fixaddr)

// CBNZ
ENTRY_GLOBAL_ARM(test_a64_cbnz)
    mov      w16, #1
    cbnz     w16, .a64_cbnz_next
    nop
    nop
    ret
.a64_cbnz_next:
    b        test_a64_helper_global
END(test_a64_cbnz)

// CBNZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_cbnz_fixaddr)
    mov      w16, #1
    cbnz     w16, .a64_cbnz_fixaddr_next
    ret
.a64_cbnz_fixaddr_next:
    b        test_a64_helper_global
END(test_a64_cbnz_fixaddr)

// TBZ
ENTRY_GLOBAL_ARM(test_a64_tbz)
    mov      x16, #0b1000
    tbz      x16, #2, .a64_tbz_next
    nop
    nop
    ret
.a64_tbz_next:
    b        test_a64_helper_global
END(test_a64_tbz)

// TBZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_tbz_fixaddr)
    mov      x16, #0b1000
    tbz      x16, #2, .a64_tbz_fixaddr_next
    ret
.a64_tbz_fixaddr_next:
    b        test_a64_helper_global
END(test_a64_tbz_fixaddr)

// TBNZ
ENTRY_GLOBAL_ARM(test_a64_tbnz)
    mov      w16, #0b1100
    tbnz     w16, #2, .a64_tbnz_next
    nop
    nop
    ret
.a64_tbnz_next:
    b        test_a64_helper_global
END(test_a64_tbnz)

// TBNZ fixaddr
ENTRY_GLOBAL_ARM(test_a64_tbnz_fixaddr)
    mov      w16, #0b1100
    tbnz     w16, #2, .a64_tbnz_fixaddr_next
    ret
.a64_tbnz_fixaddr_next:
    b        test_a64_helper_global
END(test_a64_tbnz_fixaddr)


ENTRY_HIDDEN_ARM(test_a64_helper_hidden_tail)
    add      x0, x0, x1
    ret
END(test_a64_helper_global_tail)
