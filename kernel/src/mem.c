#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "printf.h"
#include "mem.h"

uint64_t hhdm_offset = 0; // Define it globally
uint8_t* bitmap = NULL;      // The actual pointer to our bitmap array
uint64_t  total_pages = 0; // Calculated in pmm_init, used in pmm_alloc
uint64_t  bitmap_size = 0; // Needed to know how much to memset/lock
uint64_t* kernel_pml4 = NULL;


// You'll need your limine.h header for this
// Use 'volatile' to prevent the compiler from getting 'clever'
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};


typedef uint64_t pt_entry;

struct page_table {
    pt_entry entries[512]; // Each table has 512 entries (512 * 8 bytes = 4096 bytes)
};

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }

    return s;
}


void pmm_init() {
    struct limine_memmap_response *map = memmap_request.response;
    uint64_t hhdm_offset = hhdm_request.response->offset;
    uint64_t highest_addr = 0;

    // 1. Find the top of RAM
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = entry->base + entry->length;
            if (top > highest_addr) highest_addr = top;
        }
    }

    total_pages = highest_addr / 4096;
    bitmap_size = total_pages / 8;

    // 2. Find a spot for the bitmap
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bitmap_size) {
            // Virtual address for the kernel to access it
            bitmap = (uint8_t*)(entry->base + hhdm_offset);
            
            // Mark all as USED (0xFF)
            for (uint64_t j = 0; j < bitmap_size; j++) bitmap[j] = 0xFF;
            
            printf("PMM: Bitmap at %p (Phys: %x)\n", bitmap, entry->base);
            break;
        }
    }
}

void pmm_set_page(uint64_t page_addr) {
    uint64_t index = page_addr / 4096;
    bitmap[index / 8] |= (1 << (index % 8));
}

void pmm_free_page(uint64_t page_addr) {
    uint64_t index = page_addr / 4096;
    bitmap[index / 8] &= ~(1 << (index % 8));
}

void pmm_init_free_regions() { //sweep
    struct limine_memmap_response *map = memmap_request.response;
    printf("B");
    if (map == NULL) return;
    for (uint64_t i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];

        // Only free regions that Limine marks as USABLE
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            for (uint64_t offset = 0; offset < entry->length; offset += 4096) {
                pmm_free_page(entry->base + offset);
            }
        }
    }
}

uint64_t pmm_alloc() {
    // total_pages was calculated during pmm_init
    for (uint64_t i = 0; i < total_pages / 64; i++) {
        uint64_t* bulk = (uint64_t*)bitmap;
        
        // If the 64-bit chunk isn't 0xFFFFFFFFFFFFFFFF, there's a free page here
        if (bulk[i] != 0xFFFFFFFFFFFFFFFF) {
            for (int j = 0; j < 64; j++) {
                uint64_t bit = 1ULL << j;
                if (!(bulk[i] & bit)) {
                    uint64_t page_idx = i * 64 + j;
                    uint64_t addr = page_idx * 4096;
                    
                    pmm_set_page(addr); // Mark as used
                    return addr;
                }
            }
        }
    }
    return 0; // Out of memory!
}

// 3. LOCK THE BITMAP (Crucial!)
void lock_bitmap(){
    uint64_t hhdm_offset = hhdm_request.response->offset;
    uint64_t bitmap_phys = (uint64_t)bitmap - hhdm_offset;
    uint64_t pages_to_lock = (bitmap_size + 4095) / 4096;

    for (uint64_t i = 0; i < pages_to_lock; i++) {
        pmm_set_page(bitmap_phys + (i * 4096));
    }
}

void limine_check(){
    printf("Checking Limine bootloader connection...\n");

    if (memmap_request.response == NULL) {
        printf("FAILED: Memmap is NULL. Limine doesn't see us.\n");
    } else {
        printf("SUCCESS: Memmap found! Entries: %d\n", (int)memmap_request.response->entry_count);
    }
}

// We assume hhdm_offset is globally accessible from pmm_init
extern uint64_t hhdm_offset;

pt_entry* vmm_get_pte(uint64_t* pml4, uint64_t virt_addr, bool allocate) {
    pt_entry* entry;
    uint64_t* current_table = pml4;

    // Levels: PML4 (4) -> PDPT (3) -> PD (2) -> PT (1)
    // We need to traverse 3 levels to reach the final Page Table
    uint64_t indices[4] = {
        PML4_IDX(virt_addr),
        PDPT_IDX(virt_addr),
        PD_IDX(virt_addr),
        PT_IDX(virt_addr)
    };

    for (int i = 0; i < 3; i++) {
        entry = &current_table[indices[i]];

        if (!(*entry & PTE_PRESENT)) {
            if (!allocate) return NULL;
            uint64_t new_table = pmm_alloc();
            memset((void*)(new_table + hhdm_offset), 0, 4096);
            *entry = new_table | PTE_PRESENT | PTE_WRITABLE | PTE_USER;
        }

        // The next table is the address in the current entry + HHDM
        current_table = (uint64_t*)((*entry & PTE_ADDR_MASK) + hhdm_offset);
    }

    return &current_table[indices[3]]; // Return the PT entry
}

void vmm_map(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    pt_entry* pte = vmm_get_pte(pml4, virt_addr, true);
    
    // Set the entry to the physical address plus our flags
    // We mask the physical address just in case it's not page-aligned
    *pte = (phys_addr & PTE_ADDR_MASK) | flags | PTE_PRESENT;
    
    // Tell the CPU to refresh its cache (TLB) for this virtual address
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void vmm_init() {
    // 1. Ask PMM for a physical page for our new PML4 table
    uint64_t phys_pml4 = pmm_alloc(); 
    
    // 2. Convert that to a virtual address so we can talk to it
    kernel_pml4 = (uint64_t*)(phys_pml4 + hhdm_offset);
    
    // 3. Zero it out (Important!)
    memset(kernel_pml4, 0, 4096);

    // 4. Get Limine's current PML4 address from the CR3 register
    uint64_t current_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(current_cr3));
    
    // Convert Limine's physical CR3 to a virtual address we can read
    uint64_t* limine_pml4 = (uint64_t*)(current_cr3 + hhdm_offset);

    // COPY ALL 512 ENTRIES
    // This makes our new table an identical twin of Limine's table
    for (int i = 0; i < 512; i++) {
        kernel_pml4[i] = limine_pml4[i];
    }

    // 6. SWITCH: Tell the CPU to use our new table
    // We pass the PHYSICAL address to CR3
    asm volatile("mov %0, %%cr3" : : "r"(phys_pml4) : "memory");
    
    printf("VMM: Switched to new PML4 at physical %x\n", phys_pml4);
}