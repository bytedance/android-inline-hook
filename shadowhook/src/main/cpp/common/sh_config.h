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

#pragma once

/////////////////////////////////////////////////////////////////////////////////////////
// *** >>> IMPORTANT <<< ***
// Do NOT modify the following configuration unless you know exactly what you are doing.
/////////////////////////////////////////////////////////////////////////////////////////

// Global debugging. Note that in some cases these logs themselves can cause a crash.
//
// Do not enable it in a production environment !!!
//
// #define SH_CONFIG_DEBUG

// Operation record of hook and unhook.
//
// Disabling it can reduce hook/unhook latency, memory footprint and file size.
//
#define SH_CONFIG_OPERATION_RECORDS

// Crash signal protection.
//
// Do not disable it in a production environment !!!
//
#define SH_CONFIG_SIG_PROT

// Try using branch islands, so that only a single relative jump instruction
// is needed at the target address.
//
// Do not disable it in a production environment !!!
//
#define SH_CONFIG_TRY_HOOK_WITH_ISLAND

// Try not to use branch islands. In this case, you need to use multiple instructions
// to perform an absolute jump at the target address.
//
// Do not enable it in a production environment !!!
//
// #define SH_CONFIG_TRY_HOOK_WITHOUT_ISLAND

// When hooking the function of the thumb instruction, if the function is too short,
// try to use the gap aligned at the end of the function.
//
// Do not disable it in a production environment !!!
//
#define SH_CONFIG_DETECT_THUMB_TAIL_ALIGNED

// In some cases, we cannot corrupt the IP registers (x16 and x17 in arm64, r12 in arm32),
// and we can only choose one (or a group of) registers to save on the stack first,
// and then restore from the stack after use. This definition determines whether we choose
// x16 and x17 registers, or x0 and x1 registers (r12 and r0 for arm).
// This configuration is only used to facilitate testing during development.
//
// Do not disable it in a production environment !!!
//
#define SH_CONFIG_CORRUPT_IP_REGS
