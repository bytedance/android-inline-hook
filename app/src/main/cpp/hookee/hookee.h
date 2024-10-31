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

#pragma once

#if defined(__arm__)

int test_t16_helper_global(int a, int b);
int test_t16_b_t1(int a, int b);
int test_t16_b_t1_fixaddr(int a, int b);
int test_t16_b_t2(int a, int b);
int test_t16_b_t2_fixaddr(int a, int b);
int test_t16_bx_t1(int a, int b);
int test_t16_add_reg_t2(int a, int b);
int test_t16_mov_reg_t1(int a, int b);
int test_t16_adr_t1(int a, int b);
int test_t16_ldr_lit_t1(int a, int b);
int test_t16_cbz_t1(int a, int b);
int test_t16_cbz_t1_fixaddr(int a, int b);
int test_t16_cbnz_t1(int a, int b);
int test_t16_cbnz_t1_fixaddr(int a, int b);
int test_t16_it_t1_case1(int a, int b);
int test_t16_it_t1_case2(int a, int b);
int test_t16_it_t1_case3(int a, int b);

int test_t32_helper_global(int a, int b);
int test_t32_b_t3(int a, int b);
int test_t32_b_t4(int a, int b);
int test_t32_b_t4_fixaddr(int a, int b);
int test_t32_bl_imm_t1(int a, int b);
int test_t32_blx_imm_t2(int a, int b);
int test_t32_adr_t2(int a, int b);
int test_t32_adr_t3(int a, int b);
int test_t32_ldr_lit_t2_case1(int a, int b);
int test_t32_ldr_lit_t2_case2(int a, int b);
int test_t32_pld_lit_t1(int a, int b);
int test_t32_pli_lit_t3(int a, int b);
int test_t32_tbb_t1(int a, int b);
int test_t32_tbh_t1(int a, int b);
int test_t32_vldr_lit_t1_case1(int a, int b);
int test_t32_vldr_lit_t1_case2(int a, int b);

int test_a32_helper_global(int a, int b);
int test_a32_b_a1(int a, int b);
int test_a32_b_a1_fixaddr(int a, int b);
int test_a32_bx_a1(int a, int b);
int test_a32_bl_imm_a1(int a, int b);
int test_a32_blx_imm_a2(int a, int b);
int test_a32_add_reg_a1_case1(int a, int b);
int test_a32_add_reg_a1_case2(int a, int b);
int test_a32_add_reg_a1_case3(int a, int b);
int test_a32_sub_reg_a1_case1(int a, int b);
int test_a32_sub_reg_a1_case2(int a, int b);
int test_a32_sub_reg_a1_case3(int a, int b);
int test_a32_adr_a1_case1(int a, int b);
int test_a32_adr_a1_case2(int a, int b);
int test_a32_adr_a2_case1(int a, int b);
int test_a32_adr_a2_case2(int a, int b);
int test_a32_mov_reg_a1_case1(int a, int b);
int test_a32_mov_reg_a1_case2(int a, int b);
int test_a32_mov_reg_a1_case3(int a, int b);
int test_a32_ldr_lit_a1_case1(int a, int b);
int test_a32_ldr_lit_a1_case2(int a, int b);
int test_a32_ldr_reg_a1_case1(int a, int b);
int test_a32_ldr_reg_a1_case2(int a, int b);

#elif defined(__aarch64__)

int test_a64_helper_global(int a, int b);
int test_a64_b(int a, int b);
int test_a64_b_fixaddr(int a, int b);
int test_a64_b_cond(int a, int b);
int test_a64_b_cond_fixaddr(int a, int b);
int test_a64_bl(int a, int b);
int test_a64_bl_fixaddr(int a, int b);
int test_a64_adr(int a, int b);
int test_a64_adrp(int a, int b);
int test_a64_ldr_lit_32(int a, int b);
int test_a64_ldr_lit_64(int a, int b);
int test_a64_ldrsw_lit(int a, int b);
int test_a64_prfm_lit(int a, int b);
int test_a64_ldr_simd_lit_32(int a, int b);
int test_a64_ldr_simd_lit_64(int a, int b);
int test_a64_ldr_simd_lit_128(int a, int b);
int test_a64_cbz(int a, int b);
int test_a64_cbz_fixaddr(int a, int b);
int test_a64_cbnz(int a, int b);
int test_a64_cbnz_fixaddr(int a, int b);
int test_a64_tbz(int a, int b);
int test_a64_tbz_fixaddr(int a, int b);
int test_a64_tbnz(int a, int b);
int test_a64_tbnz_fixaddr(int a, int b);

#endif

int test_recursion_1(int a, int b);
int test_recursion_2(int a, int b);

int test_hook_multi_times(int a, int b);

void *get_hidden_func_addr(void);
