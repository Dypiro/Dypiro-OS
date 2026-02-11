#include <stdint.h>
#include <stddef.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "printf.h"
#include "kernel.h"
#include "main.h"
#include "gdt.h"

struct gdt_entry gdt[3];
struct gdt_ptr gdtp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access      = access;
}

void init_gdt() {
    gdtp.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gdtp.base  = (uint64_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    // Code segment: Access 0x9A, Granularity 0x20 (Long Mode bit)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xA0); 
    // Data segment: Access 0x92
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xA0); 

    gdt_flush((uint64_t)&gdtp);
}