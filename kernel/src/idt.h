#include <stdint.h>

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;    // 0x08
    uint8_t  ist;         // 0
    uint8_t  flags;       // 0x8E
    uint16_t base_mid;
    uint32_t base_high;   // The 64-bit "extension"
    uint32_t reserved;    // Must be 0
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base; // Ensure this is uint64_t
} __attribute__((packed));

// Global IDT and Pointer
extern struct idt_entry idt[256];
extern struct idt_ptr idtp;
void init_idt();