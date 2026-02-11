[bits 64]

interrupt_common_stub:
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    ; Call the C dispatcher
    mov rdi, rsp        ; Pass the stack pointer as the first argument (registers)
    extern irq_handler
    call irq_handler

    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    add rsp, 16         ; Clean up the 2 values we pushed in the specific handlers
    iretq

global timer_handler_asm
timer_handler_asm:
    push 0              ; Dummy error code
    push 32             ; Interrupt number
    jmp interrupt_common_stub

global keyboard_handler_asm
keyboard_handler_asm:
    push 0              ; Dummy error code
    push 33             ; Interrupt number
    jmp interrupt_common_stub