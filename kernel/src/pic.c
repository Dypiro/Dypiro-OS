// This could totally be in main.c i just felt like putting it in a different file, this is just a PIC remap
// Coz of conflicting addresses i have remaped IRQ's to start at 0x20 (32) instead, which is why keyboard (IRQ 1) is at index 33
// I have also implemented that it should ignore every interrupt except keyboard
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>

extern uint8_t read_port(uint16_t port);
extern void write_port(uint16_t port, uint8_t value);

static inline void io_wait() {
    write_port(0x80, 0);
}

void pic_init() {
    uint8_t a1 = read_port(0x21); // Save masks
    uint8_t a2 = read_port(0xA1);

    // ICW1: Start initialization in cascade mode
    write_port(0x20, 0x11); io_wait();
    write_port(0xA0, 0x11); io_wait();

    // ICW2: Remap Offset (The most important part)
    write_port(0x21, 0x20); io_wait(); // Master PIC -> Interrupts 0x20-0x27
    write_port(0xA1, 0x28); io_wait(); // Slave PIC  -> Interrupts 0x28-0x2F

    // ICW3: Setup cascading
    write_port(0x21, 0x04); io_wait();
    write_port(0xA1, 0x02); io_wait();

    // ICW4: Environment info (8086 mode)
    write_port(0x21, 0x01); io_wait();
    write_port(0xA1, 0x01); io_wait();

    // Mask everything except the keyboard (IRQ 1)
    // 0 = Enabled, 1 = Disabled. 0xFD is 11111101
    write_port(0x21, 0xFD); 
    write_port(0xA1, 0xFF); 
}