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
#include "mem.h"
//#include "io.h"

// A struct that matches the order of the 'push' instructions
struct registers {
    uint64_t r11, r10, r9, r8, rdi, rsi, rdx, rcx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss; // Pushed by hardware
};

void page_fault_handler(struct registers* regs);
void double_fault_handler(struct registers* regs);
extern void keyboard_handler_asm();
extern void page_fault_handler_asm();
extern void double_fault_handler_asm();
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

    idt_set_gate(14, (uint64_t)page_fault_handler_asm);

    idt_set_gate(10, (uint64_t)double_fault_handler_asm);

    // Update PIC mask to allow IRQ 0 AND IRQ 1
    write_port(0x21, 0xFC);

    // 3. Load the IDT
    __asm__ volatile("lidt %0" : : "m"(idtp));
    printf("IDT Entry Size: %d bytes\n", (int)sizeof(struct idt_entry));
    printf("IDT Loaded. Keyboard Handler at: %p\n", keyboard_handler_asm);
}


void irq_handler(struct registers* regs) {
    if (regs->int_no == 32) {
        timer_handler_c();
    } else if (regs->int_no == 33) {
        keyboard_handler_c();
    }
    else if (regs->int_no == 14) { //page fault
        page_fault_handler(regs);
    }
    else if (regs->int_no == 10) { //double fault
        double_fault_handler(regs);
    }

    // EOI (End Of Interrupt)
    write_port(0x20, 0x20);
}

void page_fault_handler(struct registers* regs) {
    uint64_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));

    // Align address to page boundary
    uint64_t aligned_addr = faulting_address & ~0xFFF;

    // Check if the fault is "Not Present" (Bit 0 is 0)
    if (!(regs->err_code & 0x1)) {
        
        /* LOGIC: Is this address supposed to be valid?
           In a real OS, you'd check your process's VMAs here.
           For a simple kernel test:
        */
        if (faulting_address >= 0x7000000000 && faulting_address < 0x8000000000) {
            uint64_t new_frame = pmm_alloc();
            if (new_frame) {
                // Map the missing page using your existing VMM
                vmm_map(kernel_pml4, aligned_addr, new_frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
                
                // Success! Return to the instruction that faulted.
                // The CPU will retry the access and succeed this time.
                return; 
            }else{
                printf("Segmentation Fault (Core Dumped) at %p\n", faulting_address);
            }
        }
    }

    // If we reach here, it's a genuine crash (e.g., NULL pointer or permission violation)
    printf("\nFATAL ERROR: Unhandled Page Fault at %p | ERROR: %x | RIP: %p\n", 
            faulting_address, regs->err_code, regs->rip);
    
    __asm__("cli");
    for(;;) __asm__("hlt");
}

void double_fault_handler(struct registers* regs){
    printf("\nDOUBLE FAULT OCCURED!!!");
    printf("\nRIP: %p CS: %x\n", regs->rip, regs->err_code);
    __asm__("cli");
    for(;;) __asm__("hlt");
}