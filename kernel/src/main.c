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

    init_gdt();
    pic_init();
    init_idt();
    __asm__ volatile("sti"); // Set Interrupt Flag

    pmm_init();               // Setup the bitmap structure
    pmm_init_free_regions();  // Mark Usable RAM as free in the bitmap

    lock_bitmap();

    vmm_init();

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

    // 4. Pass it to the VFS we built earlier
    vfs_mount_tar(tar_addr);

    // Now you can find the file!
    vfs_node_t* my_txt = vfs_open("hello.txt");

    if (my_txt != NULL) {
        // 1. Allocate a buffer to hold the text + a null terminator
        char* content = (char*)kmalloc(my_txt->size + 1);
        
        // 2. Use the VFS read function we defined
        my_txt->read(my_txt, 0, my_txt->size, (uint8_t*)content);
        
        // 3. Null-terminate so we can print it safely
        content[my_txt->size] = '\0';
        
        printf("File Found! Name: %s, Size: %d bytes\n", my_txt->name, my_txt->size);
        printf("Content: %s\n", content);
        
        kfree(content);
    } else {
        printf("Error: Could not find hello.txt in VFS!\n");
    }
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
