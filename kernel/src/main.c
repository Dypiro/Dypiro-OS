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
#include "vfs.h"
#include "tss.h"


// Define where we want the framebuffer to live in our virtual memory
#define VIDEO_VIRT_BASE 0xffffffffc0000000 

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

void user_program() {
    // Pick an address close to the stack (which we know works!)
    // If stack is at 0x7FFFFFFF000, let's try 0x7FFFFEE000
    uint64_t* ptr = (uint64_t*)0x7FFFFEE000; 
    
    // This SHOULD trigger a Page Fault (Not Present)
    // The handler should catch it, map it, and return here.
    *ptr = 0xDEADC0DE; 

    // If we reach this line, the OS is officially handling demand paging!
    while(1); 
}


static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0
};


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

    pmm_init();               // Setup the bitmap structure
    pmm_init_free_regions();  // Mark Usable RAM as free in the bitmap

    lock_bitmap();

    vmm_init();

    init_gdt();
    pic_init();
    init_idt();
    __asm__ volatile("sti"); // Set Interrupt Flag

    // 2. Get the RAW Physical Address
    // Limine's address is Virtual (HHDM). We need Physical for vmm_map.
    uint64_t fb_phys = (uint64_t)framebuffer->address - hhdm_offset;

    // 3. Calculate total size to map (Width * Pitch is safer than Width * Height)
    uint64_t fb_size_bytes = framebuffer->height * framebuffer->pitch;

    // 4. Map the entire framebuffer into our NEW address space
    // We loop page-by-page (4096 bytes)
    for (uint64_t i = 0; i < fb_size_bytes; i += 4096) {
        vmm_map(
            kernel_pml4,           // The new PML4
            VIDEO_VIRT_BASE + i,   // Our chosen Virtual Address
            fb_phys + i,           // The actual Physical hardware address
            PTE_PRESENT | PTE_WRITABLE
        );
    }

    // 5. Initialize flanterm using OUR virtual address
    // Instead of framebuffer->address, we pass VIDEO_VIRT_BASE
    ft_ctx = flanterm_fb_simple_init(
        (void*)VIDEO_VIRT_BASE, 
        framebuffer->width, 
        framebuffer->height, 
        framebuffer->pitch
    );

    printf("Framebuffer remapped!\n");

    /*void* p1 = kmalloc(128);
    void* p2 = kmalloc(1024 * 10); // 10KB, should trigger multiple vmm_maps
    void* p3 = kmalloc(16);

    printf("Alloc 1 (128b): %p\n", p1);
    printf("Alloc 2 (10kb): %p\n", p2);
    printf("Alloc 3 (16b):  %p\n", p3);

    // Try writing to them to ensure they are actually mapped!
    memcpy(p1, "ABCDEFGHIJ", sizeof("ABCDEFGHIJ"));

    printf("Test string in heap: %s\n", (char*)p1);

    printf("Freeing middle block (%p)...\n", p2);
    kfree(p2);

    void* p4 = kmalloc(1024 * 10);
    printf("Newly allocated block:    %p\n", p4);

    if (p4 == p2) {
        printf("SUCCESS: Memory recycled perfectly! Although this probably indicates that something is wrong\n");
    } else {
        printf("FAILURE: Memory was not recycled. Check your free_list logic.\n");
    }

    kfree(p1);
    printf("%s\n",p1);
    void* p5 = kmalloc(128);

    if (p5 == p1) {
        printf("SUCCESS: Memory recycled perfectly!\n");
    } else {
        printf("FAILURE: Memory was not recycled. Check your free_list logic.\n");
    }*/
    if (module_request.response == NULL || module_request.response->module_count < 1) {
        // Halt or Error: No modules found!
        hcf(); 
    }

    // 2. Grab the first module (our TAR)
    struct limine_file* tar_module = module_request.response->modules[0];

    // 3. Get the pointer and size
    void* tar_addr = tar_module->address;
    uint64_t tar_size = tar_module->size;
    
    // 4. Pass it to the VFS
    vfs_mount_tar(tar_addr);

    /*vfs_node_t* n1 = vfs_open("bin/meh.txt");
    vfs_node_t* n2 = vfs_open("hello.txt");

    if (n1 == n2) {
        printf("BUG DETECTED: vfs_open returned the same pointer for both files!\n");
    } else {
        printf("Nodes are distinct. Checking data pointers...\n");
        if (n1->private_data == n2->private_data) {
            printf("BUG DETECTED: Both files point to the same memory: %p\n", n1->private_data);
        }
    }
    if (n1) {
        printf("N1 Found: %s\n", n1->name);
    } else {
        printf("N1 NOT FOUND in VFS!\n");
    }
    if (n2) {
    // 1. Get a physical frame for the code
    uint64_t phys_code = pmm_alloc()
        printf("N2 Found: %s\n", n2->name);
    } else {
        printf("N2 NOT FOUND in VFS!\n");
    }*/

    /*uint64_t* demand_ptr = (uint64_t*)0x7000001000;

    printf("Attempting access to unmapped memory...\n");

    // This triggers the #PF, handler maps it, and returns here
    *demand_ptr = 0x12345; 

    printf("If you see this, Demand Paging worked! Value: %x\n", *demand_ptr);*/


    // 1. Get a physical frame for the code
    uint64_t phys_code = pmm_alloc();

    // 2. Map it to a USER-space virtual address (Lower Half)
    // IMPORTANT: Ensure PTE_USER (0x04) is passed!
    uint64_t user_virt_rip = 0x400000; 
    vmm_map(kernel_pml4, user_virt_rip, phys_code, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 3. Copy the function to that physical frame
    // Since your kernel is in the higher half, you need your HHDM offset 
    // to touch the physical memory directly.
    uint8_t* destination = (uint8_t*)(phys_code + hhdm_offset);
    memcpy(destination, user_program, 1024); // Copy the function code

    // 4. Do the same for the stack
    uint64_t phys_stack = pmm_alloc();
    uint64_t user_virt_rsp = 0x7FFFFFFF000;
    vmm_map(kernel_pml4, user_virt_rsp, phys_stack, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    // 5. JUMP!
    test_user_entry(user_virt_rsp + 4096, user_virt_rip);



    printf(">");
    kmain();
    // We're done, just hang...
    hcf();
}

/*
example usage of kmalloc
struct process* p = kmalloc(sizeof(struct process));

struct node {
    void* data;
    struct node* next;
};
struct node* n = kmalloc(sizeof(struct node));
*/
