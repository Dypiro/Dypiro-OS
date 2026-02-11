#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Each GDT entry is 8 bytes
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// The pointer structure passed to the LGDT instruction
struct gdt_ptr {
    uint16_t limit;
    uint64_t base; // 64-bit base address for Long Mode
} __attribute__((packed));

// Standard Access Byte flags
#define GDT_ACCESS_PRESENT     0x80 // Must be 1
#define GDT_ACCESS_RING0       0x00
#define GDT_ACCESS_RING3       0x60
#define GDT_ACCESS_CODE_DATA   0x10 // 1 for code/data, 0 for TSS/LDT
#define GDT_ACCESS_EXECUTABLE  0x08 // 1 for code, 0 for data
#define GDT_ACCESS_WRITABLE    0x02 // Allow writing for data, reading for code

// Granularity/Flags bits
#define GDT_FLAG_64BIT         0x20 // The 'L' bit: defines 64-bit code segment
#define GDT_FLAG_32BIT         0x40 // The 'D/B' bit
#define GDT_FLAG_GRANULARITY   0x80 // 4KB blocks if set

void init_gdt();
extern void gdt_flush(uint64_t gdt_ptr_addr);

#endif