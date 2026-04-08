#include <stdint.h> //I decided to use this for tss as well
#include <stddef.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "printf.h"
#include "kernel.h"
#include "gdt.h"
#include "mem.h"

struct gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper32;
    uint32_t reserved;
} __attribute__((packed));

// Expanded GDT: 0=Null, 1=KCode, 2=KData, 3=UCode, 4=UData, 5=TSS(16 bytes)
// Because TSS is 16 bytes, it takes up the space of TWO slots (5 and 6).
struct gdt_entry gdt[7]; 

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;      // This is the stack the CPU switches to on an interrupt
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint32_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed)) my_tss;

struct tss_entry my_tss;

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

struct gdt_ptr gdtp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access      = access;
}

void gdt_set_tss(int num, uint64_t base, uint32_t limit) {
    struct gdt_tss_entry* tss_desc = (struct gdt_tss_entry*)&gdt[num];
    
    tss_desc->limit_low    = (limit & 0xFFFF);
    tss_desc->base_low     = (base & 0xFFFF);
    tss_desc->base_mid     = (base >> 16) & 0xFF;
    tss_desc->access       = 0x89; // Present, Executable, TSS
    tss_desc->base_high    = (base >> 24) & 0xFF;
    tss_desc->base_upper32 = (base >> 32) & 0xFFFFFFFF;
    tss_desc->granularity  = ((limit >> 16) & 0x0F);
    tss_desc->reserved     = 0;
}

void init_gdt() {
    gdtp.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gdtp.base  = (uint64_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xA0); // Kernel Code (0x08)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xA0); // Kernel Data (0x10)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xA0); // User Code   (0x18 | 3 = 0x1B)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xA0); // User Data   (0x20 | 3 = 0x23)

    // Setup TSS
    uint64_t tss_base = (uint64_t)&my_tss;
    my_tss.rsp0 = (uint64_t)pmm_alloc() + 4096 + hhdm_offset; // Allocate a stack for the kernel
    gdt_set_tss(5, tss_base, sizeof(struct tss_entry) - 1);

    gdt_flush((uint64_t)&gdtp);

    // Load Task Register (Points to GDT entry 5, which is offset 0x28)
    __asm__ volatile("ltr %%ax" : : "a" (0x28));

}