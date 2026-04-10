#include <stdint.h>
#include <stddef.h>
#include "printf.h"
#include "kernel.h"
#include "gdt.h"
#include "mem.h"
#include "tss.h"



void test_user_entry(uint64_t stack_ptr, uint64_t entry_ptr) {
    printf("Transitioning to Ring 3...\n");

    __asm__ volatile( //this is a simplified assembly concept
        "cli \n\t"               // Disable interrupts during transition
        "mov $0x23, %%ax \n\t"   // User Data Segment (0x20 | 3)
        "mov %%ax, %%ds \n\t"
        "mov %%ax, %%es \n\t"
        "mov %%ax, %%fs \n\t"
        "mov %%ax, %%gs \n\t"
        
        "pushq $0x23 \n\t"       // SS (User Data)
        "pushq %0 \n\t"          // RSP
        "pushq $0x202 \n\t"      // RFLAGS (IF=1)
        "pushq $0x1B \n\t"       // CS (User Code: 0x18 | 3)
        "pushq %1 \n\t"          // RIP
        "iretq \n\t"
        : : "r" (stack_ptr), "r" (entry_ptr) : "memory"
    );
}