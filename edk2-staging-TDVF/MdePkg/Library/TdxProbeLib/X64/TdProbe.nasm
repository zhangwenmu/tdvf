;------------------------------------------------------------------------------
;*
;* Copyright (c) 2021, Intel Corporation. All rights reserved.<BR>
;* SPDX-License-Identifier: BSD-2-Clause-Patent
;*
;*
;------------------------------------------------------------------------------

DEFAULT REL
SECTION .text

%define TD_PROBE_TD_GUEST             0
%define TD_PROBE_NOT_INTEL            1
%define TD_PROBE_NOT_PARA_VIRTUALIZED 2
%define TD_PROBE_LEGACY_GUEST         3
%define TD_PROBE_NON_TD_GUEST         4

%macro td_push_regs 0
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

%macro td_pop_regs 0
    pop rdi
    pop rsi
    pop rbx
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
%endmacro


global ASM_PFX(TdProbe)
ASM_PFX(TdProbe):

    td_push_regs

    ;
    ; CPUID (0)
    ;
    mov     eax, 0
    cpuid
    cmp     ebx, 0x756e6547  ; "Genu"
    jne     .not_intel
    cmp     edx, 0x49656e69  ; "ineI"
    jne     .not_intel
    cmp     ecx, 0x6c65746e  ; "ntel"
    jne     .not_intel

    ;
    ; CPUID (1)
    ;
    mov     eax, 1
    cpuid
    test    ecx, 0x80000000
    jz      .not_para_virtualized

    ;
    ; CPUID[0].EAX >= 0x21?
    ;
    mov     eax, 0
    cpuid
    cmp     eax, 0x21
    jl      .legacy_guest

    ;
    ; CPUID (0x21,0)
    ;
    mov     eax, 0x21
    mov     ecx, 0
    cpuid

    cmp     ebx, 0x65746E49   ; "Inte"
    jne     .non_td
    cmp     edx, 0x5844546C   ; "lTDX"
    jne     .non_td
    cmp     ecx, 0x20202020   ; "    "
    jne     .non_td

    mov     rax, TD_PROBE_TD_GUEST
    jmp     .exit

.not_intel:
    mov     rax, TD_PROBE_NOT_INTEL
    jmp     .exit

.not_para_virtualized:
    mov     rax, TD_PROBE_NOT_PARA_VIRTUALIZED
    jmp     .exit

.legacy_guest:
    mov     rax, TD_PROBE_LEGACY_GUEST
    jmp     .exit

.non_td:
    mov     rax, TD_PROBE_NON_TD_GUEST
    jmp     .exit

.exit:
    td_pop_regs
    ret

