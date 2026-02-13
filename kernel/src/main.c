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
#include "pic.h"
#include "mem.h"

void outb8(uint16_t port, uint8_t value) {
    asm("outb %1, %0" : : "dN" (port), "a" (value));
}
uint8_t inb8(uint16_t port) {
    uint8_t r;
    asm("inb %1, %0" : "=a" (r) : "dN" (port));
    return r;
}

extern uint8_t read_port(uint16_t port);
extern void write_port(uint16_t port, uint8_t value);





struct flanterm_context *ft_ctx;
// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

static volatile LIMINE_BASE_REVISION(1);
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};
// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once.


// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
// They CAN be moved to a different .c file.

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    for (size_t i = 0; i < n; i++) {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest) {
        for (size_t i = 0; i < n; i++) {
            pdest[i] = psrc[i];
        }
    } else if (src < dest) {
        for (size_t i = n; i > 0; i--) {
            pdest[i-1] = psrc[i-1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

// Halt and catch fire function.
static void hcf(void) {
    asm ("cli");
    for (;;) {
        asm ("hlt");
    }
}
/* This will continuously loop until the given time has
*  been reached */
#define PIT_CHANNEL0_DATA_PORT 0x40
#define PIT_COMMAND_PORT       0x43
#define PIT_FREQUENCY          1193182  // PIT operates at 1.193182 MHz

/*void timer_init() {
    // Send command byte to PIT
    outb8(PIT_COMMAND_PORT, 0x36); // 0x36 sets channel 0, access mode, and operating mode
}*/ 

/*void test_pmm() {
    printf("test pmm engaged");
    uint64_t addr1 = pmm_alloc();
    uint64_t addr2 = pmm_alloc();
    printf("\nAllocated: %x, %x", addr1, addr2);
    
    pmm_free_page(addr1);
    uint64_t addr3 = pmm_alloc();
    printf("\nAfter free, new alloc: %x", addr3); 
    // addr3 should equal addr1 if your search starts from the beginning!
}*/

// The following will be our kernel's entry point.
// If renaming _start() to something else, make sure to change the
// linker script accordingly.
void _start(void) {
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    ft_ctx = flanterm_fb_simple_init(
    framebuffer->address, framebuffer->width, framebuffer->height, framebuffer->pitch
    );
    init_gdt();
    pic_init();
    init_idt();
    __asm__ volatile("sti"); // Set Interrupt Flag

    limine_check();

    printf("Initializing PMM...\n");
    
    pmm_init();               // 1. Setup the bitmap structure
    pmm_init_free_regions();  // 2. Mark Usable RAM as free in the bitmap

    lock_bitmap();

    // Get physical address by subtracting the offset
    printf("PMM Ready. Testing allocation...\n");
    
    // 4. THE TEST
    uint64_t page1 = pmm_alloc();
    uint64_t page2 = pmm_alloc();
    
    printf("Page 1: %x\n", page1);
    printf("Page 2: %x\n", page2);

    //vmm_init(); // Switch to our own page tables

    uint64_t phys = pmm_alloc();
    uint64_t virt = 0x1234567000;

    // Use the global kernel_pml4 we just set up
    //vmm_map(kernel_pml4, virt, phys, PTE_PRESENT | PTE_WRITABLE);

    /*uint64_t* ptr = (uint64_t*)virt;
    *ptr = 0xABCDE;

    if (*ptr == 0xABCDE) {
        printf("Success! Virtual address %x is live.\n", virt);
    }*/
    printf(">");
    kmain();
    // We're done, just hang...
    hcf();
}
