[bits 64]
extern keyboard_handler_c
global keyboard_handler_asm

keyboard_handler_asm: ; This is considered an ISR Entry point
    ; 1. Save the volatile registers (the ones C might change)
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11

    ; 2. Call your C logic
    call keyboard_handler_c

    ; 3. Restore the registers in exact reverse order
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax

    ; 4. Return to the interrupted code
    iretq