; SMP AP trampoline
; Assembled as a flat binary loaded at TRAMPOLINE_PAGE (0x7000).
; The BSP writes configuration data + GDT to the info structure before sending SIPIs.
;
; Execution flow:
;   real mode entry (SIPI vector) -> protected mode -> long mode -> C entry

org 0x7000
bits 16

; ---------------------------------------------------------------------
; Entry point — SIPI vector sends us here with CS:IP = 0x0000:0x7000
; ---------------------------------------------------------------------
smp_trampoline_start:
    jmp short _entry

; ---------------------------------------------------------------------
; Info structure — BSP fills these fields before SIPI.
; Offsets must match the C constants in smp.cpp.
; ---------------------------------------------------------------------
info:
.gdt_limit_kernel:   dw 0       ; +0x02  kernel GDT limit (for switch in long mode)
.gdt_base_kernel:    dq 0       ; +0x04  kernel GDT base  (virtual)
.cr3:                dq 0       ; +0x0C  AP PML4 phys addr
.stack_top:          dq 0       ; +0x14  AP stack top     (virtual)
.entry_fn:           dq 0       ; +0x1C  C entry function (virtual)
.cpu_id:             dd 0       ; +0x24  AP index

; BSP writes the GDTR and GDT entries below before sending SIPIs.
.gdtr:               dw 0       ; +0x28  2-byte limit
                     dd 0       ; +0x2A  4-byte base (physical addr of .gdt_entries)
.gdt_entries:        times 32 db 0 ; +0x2E  null, code32, data32, code64

; ---------------------------------------------------------------------
; Real-mode entry
; ---------------------------------------------------------------------
_entry:
    cli
    cld

    ; Discover our physical base address (CS=0, IP = physical addr)
    call _get_ip
_get_ip:
    pop bx
    sub bx, (_get_ip - smp_trampoline_start)
    ; bx = page base (e.g. 0x7000)

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Load GDT (GDTR + entries already written by BSP)
    lgdt [bx + (info.gdtr - smp_trampoline_start)]

    ; Enable protected mode
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Far jump into 32-bit mode (selector 0x08 = code32)
    push dword 0x08
    movzx eax, bx
    add eax, (_pm_entry - smp_trampoline_start)
    push eax
    o32 retf

; ---------------------------------------------------------------------
; Protected-mode / compatibility-mode entry
; ---------------------------------------------------------------------
bits 32
_pm_entry:
    mov ax, 0x10                        ; data32 selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; Re-discover our base (linear == physical without paging)
    call _get_ip32
_get_ip32:
    pop ebx
    sub ebx, (_get_ip32 - smp_trampoline_start)

    ; Small stack for the transition
    lea esp, [ebx + 0xFFC]

    ; Enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; Load CR3 (AP's PML4 phys addr)
    mov eax, [ebx + (info.cr3 - smp_trampoline_start)]
    mov cr3, eax

    ; Enable long mode (EFER.LME = 1)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Enable paging — now in IA-32e compatibility mode
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ; Far jump into 64-bit mode (selector 0x18 = code64 in our temp GDT)
    push dword 0x18
    mov eax, ebx
    add eax, (_lm_entry - smp_trampoline_start)
    push eax
    retf

; ---------------------------------------------------------------------
; Long-mode entry
; ---------------------------------------------------------------------
bits 64
_lm_entry:
    ; Load kernel GDT (full 64-bit pointer from info struct)
    movzx rbx, ebx
    lgdt [rbx + (info.gdt_limit_kernel - smp_trampoline_start)]

    ; Reload CS with kernel's code64 selector (0x08)
    push 0x08
    lea rax, [rbx + (_64bit - smp_trampoline_start)]
    push rax
    retfq

_64bit:
    ; Set up segments
    mov ax, 0x10                        ; kernel data selector
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; Set up per-CPU stack
    mov rsp, [rbx + (info.stack_top - smp_trampoline_start)]
    and rsp, -16

    ; Call C entry
    mov rax, [rbx + (info.entry_fn - smp_trampoline_start)]
    mov edi, [rbx + (info.cpu_id - smp_trampoline_start)]
    call rax

.halt:
    hlt
    jmp .halt
