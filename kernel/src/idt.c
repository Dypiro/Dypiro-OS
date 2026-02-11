#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "kernel.h"
#include "printf.h"
#include "gdt.h"
#include "idt.h"
//#include "io.h"
extern void keyboard_handler_asm();
extern uint8_t read_port(uint16_t port);
extern void write_port(uint16_t port, uint8_t value);
struct idt_entry idt[256];
struct idt_ptr idtp;

void idt_set_gate(uint8_t num, uint64_t base) {
    idt[num].base_low  = (uint16_t)(base & 0xFFFF);
    idt[num].base_mid  = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].base_high = (uint32_t)((base >> 32) & 0xFFFFFFFF); // This captures the FFFFFFFF
    
    idt[num].selector  = 0x08; 
    idt[num].flags     = 0x8E; 
    idt[num].ist       = 0;
    idt[num].reserved  = 0; // The 32-bit zero field in the 16-byte entry
}
void init_idt() {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint64_t)&idt;

    // 1. Zero out everything first
    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, 0); // Only 2 args now
    }

    // 2. Map Keyboard (IRQ1 -> 0x21)
    extern void keyboard_handler_asm();
    idt_set_gate(33, (uint64_t)keyboard_handler_asm);

    // Map Timer (IRQ0 -> 0x20)
    extern void timer_handler_asm();
    idt_set_gate(32, (uint64_t)timer_handler_asm);

    // Update PIC mask to allow IRQ 0 AND IRQ 1
    write_port(0x21, 0xFC);

    // 3. Load the IDT
    __asm__ volatile("lidt %0" : : "m"(idtp));
    printf("IDT Entry Size: %d bytes\n", (int)sizeof(struct idt_entry));
    printf("IDT Loaded. Keyboard Handler at: %p\n", keyboard_handler_asm);
}

// A struct that matches the order of your 'push' instructions
struct registers {
    uint64_t r11, r10, r9, r8, rdi, rsi, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss; // Pushed by hardware
};

void irq_handler(struct registers* regs) {
    if (regs->int_no == 32) {
        timer_handler_c();
    } else if (regs->int_no == 33) {
        keyboard_handler_c();
    }
    
    // You can even send the EOI here once for everyone!
    write_port(0x20, 0x20);
}