;------------------------------------------------------------------------------
;*
;* Copyright (c) 2020 - 2021, Intel Corporation. All rights reserved.<BR>
;* SPDX-License-Identifier: BSD-2-Clause-Patent
;*
;*
;------------------------------------------------------------------------------

DEFAULT REL
SECTION .text

%define TDVMCALL_EXPOSE_REGS_MASK       0xffec
%define TDVMCALL                        0x0
%define EXIT_REASON_CPUID               0xa

%macro tdcall 0
%if (FixedPcdGet32 (PcdUseTdxEmulation) != 0)
    vmcall
%else
    db 0x66,0x0f,0x01,0xcc
%endif
%endmacro

%macro tdcall_push_regs 0
    push rbp
    mov  rbp, rsp
    push r15
    push r14
    push r13
    push r12
    push rbx
    push rsi
    push rdi
%endmacro

%macro tdcall_pop_regs 0
    pop rdi
    pop rsi
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
%endmacro

%define number_of_regs_pushed 8
%define number_of_parameters  4

;
; Keep these in sync for push_regs/pop_regs, code below
; uses them to find 5th or greater parameters
;
%define first_variable_on_stack_offset \
  ((number_of_regs_pushed * 8) + (number_of_parameters * 8) + 8)
%define second_variable_on_stack_offset \
  ((first_variable_on_stack_offset) + 8)

%macro tdcall_regs_preamble 2
    mov rax, %1

    mov ecx, %2

    ; R10 = 0 (standard TDVMCALL)

    xor r10d, r10d

    ; Zero out unused (for standard TDVMCALL) registers to avoid leaking
    ; secrets to the VMM.

    xor ebx, ebx
    xor esi, esi
    xor edi, edi

    xor edx, edx
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
%endmacro

%macro tdcall_regs_postamble 0
    xor ebx, ebx
    xor esi, esi
    xor edi, edi

    xor ecx, ecx
    xor edx, edx
    xor r8d,  r8d
    xor r9d,  r9d
    xor r10d, r10d
    xor r11d, r11d
%endmacro

;------------------------------------------------------------------------------
; 0   => RAX = TDCALL leaf
; M   => RCX = TDVMCALL register behavior
; 1   => R10 = standard vs. vendor
; RDI => R11 = TDVMCALL function / nr
; RSI =  R12 = p1
; RDX => R13 = p2
; RCX => R14 = p3
; R8  => R15 = p4

;  UINT64
;  EFIAPI
;  TdVmCall (
;    UINT64  Leaf,  // Rcx
;    UINT64  P1,  // Rdx
;    UINT64  P2,  // R8
;    UINT64  P3,  // R9
;    UINT64  P4,  // rsp + 0x28
;    UINT64  *Val // rsp + 0x30
;    )
global ASM_PFX(TdVmCall)
ASM_PFX(TdVmCall):
       tdcall_push_regs

       mov r11, rcx
       mov r12, rdx
       mov r13, r8
       mov r14, r9
       mov r15, [rsp + first_variable_on_stack_offset ]

       tdcall_regs_preamble TDVMCALL, TDVMCALL_EXPOSE_REGS_MASK

       tdcall

       ; ignore return dataif TDCALL reports failure.
       test rax, rax
       jnz .no_return_data

       ; Propagate TDVMCALL success/failure to return value.
       mov rax, r10

       ; Retrieve the Val pointer.
       mov r9, [rsp + second_variable_on_stack_offset ]
       test r9, r9
       jz .no_return_data

       ; On success, propagate TDVMCALL output value to output param
       test rax, rax
       jnz .no_return_data
       mov [r9], r11
.no_return_data:
       tdcall_regs_postamble

       tdcall_pop_regs

       ret

;------------------------------------------------------------------------------
; 0   => RAX = TDCALL leaf
; M   => RCX = TDVMCALL register behavior
; 1   => R10 = standard vs. vendor
; RDI => R11 = TDVMCALL function / nr
; RSI =  R12 = p1
; RDX => R13 = p2
; RCX => R14 = p3
; R8  => R15 = p4

;  UINT64
;  EFIAPI
;  TdVmCallCpuid (
;    UINT64  EaxIn,  // Rcx
;    UINT64  EcxIn,  // Rdx
;    UINT64  *Results  // R8
;    )
global ASM_PFX(TdVmCallCpuid)
ASM_PFX(TdVmCallCpuid):
       tdcall_push_regs

       mov r11, EXIT_REASON_CPUID
       mov r12, rcx
       mov r13, rdx

       ; Save *results pointers
       push r8

       tdcall_regs_preamble TDVMCALL, TDVMCALL_EXPOSE_REGS_MASK

       tdcall

       ; Panic if TDCALL reports failure.
       test rax, rax
       jnz .no_return_data

       ; Propagate TDVMCALL success/failure to return value.
       mov rax, r10
       test rax, rax
       jnz .no_return_data

       ; Retrieve *Results
       pop r8
       test r8, r8
       jz .no_return_data
       ; Caller pass in buffer so store results r12-r15 contains eax-edx
       mov [r8 +  0], r12
       mov [r8 +  8], r13
       mov [r8 + 16], r14
       mov [r8 + 24], r15

.no_return_data:
       tdcall_regs_postamble

       tdcall_pop_regs

       ret

.panic:
       ud2
