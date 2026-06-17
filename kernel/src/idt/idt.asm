section .text

extern isr_handler

; Save all general-purpose registers (must match interrupt_frame order)
%macro push_all_gprs 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro pop_all_gprs 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

; Macro for exception stubs (vectors 0-31)
; %2 = 1 if CPU pushes an error code, 0 otherwise
%macro isr_stub 2
global isr%1
isr%1:
    %if %2 == 0
        push 0
    %endif
    push %1
    jmp isr_common
%endmacro

; CPU exceptions 0-31
; Vectors with error code: 8, 10, 11, 12, 13, 14, 17, 21, 29, 30
isr_stub 0, 0
isr_stub 1, 0
isr_stub 2, 0
isr_stub 3, 0
isr_stub 4, 0
isr_stub 5, 0
isr_stub 6, 0
isr_stub 7, 0
isr_stub 8, 1
isr_stub 9, 0
isr_stub 10, 1
isr_stub 11, 1
isr_stub 12, 1
isr_stub 13, 1
isr_stub 14, 1
isr_stub 15, 0
isr_stub 16, 0
isr_stub 17, 1
isr_stub 18, 0
isr_stub 19, 0
isr_stub 20, 0
isr_stub 21, 1
isr_stub 22, 0
isr_stub 23, 0
isr_stub 24, 0
isr_stub 25, 0
isr_stub 26, 0
isr_stub 27, 0
isr_stub 28, 0
isr_stub 29, 1
isr_stub 30, 1
isr_stub 31, 0

; Generic stub for unused vectors (48-255)
global isr_unhandled
isr_unhandled:
    push 0
    push 0xFF
    jmp isr_common

; Macro for hardware IRQ stubs (mapped to vectors 32-47)
%macro irq_stub 1
global irq%1
irq%1:
    push 0
    push %1 + 32
    jmp isr_common
%endmacro

irq_stub 0
irq_stub 1
irq_stub 2
irq_stub 3
irq_stub 4
irq_stub 5
irq_stub 6
irq_stub 7
irq_stub 8
irq_stub 9
irq_stub 10
irq_stub 11
irq_stub 12
irq_stub 13
irq_stub 14
irq_stub 15

isr_common:
    push_all_gprs

    mov rdi, rsp
    call isr_handler
    mov rsp, rax

    pop_all_gprs
    add rsp, 16
    iretq
