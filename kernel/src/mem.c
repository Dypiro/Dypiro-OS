#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "printf.h"
#include "mem.h"

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

uint64_t hhdm_offset = 0; // Define it globally
uint8_t* bitmap = NULL;      // The actual pointer to our bitmap array
uint64_t  total_pages = 0; // Calculated in pmm_init, used in pmm_alloc
uint64_t  bitmap_size = 0; // Needed to know how much to memset/lock
uint64_t* kernel_pml4 = NULL;

#define HEAP_START 0xffffa00000000000
uint64_t heap_ptr = HEAP_START;        // The "Bump" pointer
uint64_t heap_mapped_limit = HEAP_START; // How far we've actually mapped with vmm_map

typedef struct malloc_header {
    uint64_t size;
    struct malloc_header* next;
    bool free;
} malloc_header_t;

#define HEADER_SIZE sizeof(malloc_header_t)
#define NUM_CACHES (sizeof(kmalloc_caches) / sizeof(slab_cache_t))

struct slab_cache;

typedef struct slab_header {
    uint32_t magic;                // Identifies if it's multiple pages allocated or nah
    struct slab_header* next_slab; // Link to another page of the same slot size
    struct slab_cache* parent_cache;// Uhh
    void* free_list_head;          // Pointer to the first available slot in this page
    uint32_t used_slots;           // How many are currently allocated
    uint32_t total_slots;          // How many fit in this page
    uint32_t slot_size;            // e.g., 32, 64, 128...
} slab_header_t;

typedef struct slab_cache{
    uint32_t slot_size;
    slab_header_t* partial_slabs; // Slabs with some space left
    slab_header_t* full_slabs;    // Slabs that are 100% full
    slab_header_t* empty_slabs;   // Slabs that are 100% free (ready to be returned to PMM)
} slab_cache_t;

// Standard kernel buckets
slab_cache_t kmalloc_caches[] = {
    {32, NULL, NULL, NULL},
    {64, NULL, NULL, NULL},
    {128, NULL, NULL, NULL},
    {256, NULL, NULL, NULL},
    {512, NULL, NULL, NULL},
    {1024, NULL, NULL, NULL},
    {2048, NULL, NULL, NULL},
    {4096,  NULL, NULL}, // 1 Page
    {8192,  NULL, NULL}, // 2 Pages
    {16384, NULL, NULL}, // 4 Pages
    {10240, NULL, NULL}  // 10 Pages (why) also btw equivalent to 10Kb of RAM which is more than enough, if you need more then you're mentally ill
};

// You'll need the limine.h header for this
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
    hhdm_offset = hhdm_request.response->offset;
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

void init_new_slab(slab_cache_t* cache, void* page_start) {
    slab_header_t* header = (slab_header_t*)page_start;
    // THE MAGIC STAMP
    header->magic = 0xCAFEBABE;
    header->parent_cache = cache;
    header->slot_size = cache->slot_size;
    
    // How many 4KB pages does ONE slot need?
    uint64_t pages_per_slab = (cache->slot_size > 4096) ? (cache->slot_size / 4096) : 1;
    
    // For simplicity in a multi-page bucket, we'll just put ONE slot per slab
    // if the size is > 2048. This avoids complex "spanning" logic.
    if (cache->slot_size > 2048) {
        header->total_slots = 1;
        // Map the extra pages needed for this one big slot
        for (uint64_t i = 1; i < pages_per_slab; i++) {
            uint64_t phys = pmm_alloc();
            vmm_map(kernel_pml4, (uintptr_t)page_start + (i * 4096), phys, PTE_PRESENT | PTE_WRITABLE);
        }
    } else {
        header->total_slots = (4096 - sizeof(slab_header_t)) / cache->slot_size;
    }

    header->used_slots = 0;
    
    // Start the free list right after the header
    uint8_t* first_slot = (uint8_t*)page_start + sizeof(slab_header_t);
    header->free_list_head = first_slot;

    // Stitching: Each slot stores the address of the next slot
    uint8_t* current = first_slot;
    for (uint32_t i = 0; i < header->total_slots - 1; i++) {
        uint8_t* next = current + cache->slot_size;
        *(void**)current = next; // Write the "next" pointer into the current slot
        current = next;
    }
    
    // The last slot points to NULL
    *(void**)current = NULL;

    // Add this new slab to the cache's partial list
    header->next_slab = cache->partial_slabs;
    cache->partial_slabs = header;
}

void remove_slab_from_list(slab_header_t** list_head, slab_header_t* slab_to_remove) {
    if (*list_head == NULL || slab_to_remove == NULL) return;

    if (*list_head == slab_to_remove) {
        *list_head = slab_to_remove->next_slab;
        return;
    }

    slab_header_t* current = *list_head;
    while (current->next_slab != NULL && current->next_slab != slab_to_remove) {
        current = current->next_slab;
    }

    if (current->next_slab == slab_to_remove) {
        current->next_slab = slab_to_remove->next_slab;
    }
}

void move_slab_to_full(slab_cache_t* cache, slab_header_t* slab) {
    // 1. Snip it out of the partial list
    remove_slab_from_list(&cache->partial_slabs, slab);

    // 2. Add it to the front of the full list
    slab->next_slab = cache->full_slabs;
    cache->full_slabs = slab;
}

void move_slab_to_partial(slab_cache_t* cache, slab_header_t* slab) {
    // 1. Snip it out of the full list
    remove_slab_from_list(&cache->full_slabs, slab);

    // 2. Add it back to the partial list so kmalloc can see it
    slab->next_slab = cache->partial_slabs;
    cache->partial_slabs = slab;
}

void* slab_alloc(slab_cache_t* cache) {
    if (cache->partial_slabs == NULL) {
        // No room! Allocate a new page via PMM/VMM
        uint64_t new_page_phys = pmm_alloc();
        void* new_page_virt = (void*)(new_page_phys + hhdm_offset); // Use HHDM for metadata
        
        // Setup the header and link the slots...
        init_new_slab(cache, new_page_virt);
    }
    
    slab_header_t* slab = cache->partial_slabs;
    void* slot = slab->free_list_head;
    
    // The "Next" pointer is literally stored INSIDE the free slot itself
    slab->free_list_head = *(void**)slot; 
    slab->used_slots++;
    
    // If full, move to full_slabs list
    if (slab->used_slots == slab->total_slots) {
        move_slab_to_full(cache, slab);
    }
    
    return slot;
}


void* kmalloc(uint64_t size) {
    for (int i = 0; i < NUM_CACHES; i++) {
        if (size <= kmalloc_caches[i].slot_size) {
            return slab_alloc(&kmalloc_caches[i]);
        }
    }
    printf("KMALLOC: Request %d too large for any bucket!\n", size);
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    slab_header_t* slab = (slab_header_t*)((uintptr_t)ptr & ~0xFFF);
    
    if (slab->magic == 0xCAFEBABE) {
        // Normal slab recycling logic
        *(void**)ptr = slab->free_list_head;
        slab->free_list_head = ptr;
        slab->used_slots--;
        if (slab->used_slots == slab->total_slots - 1) {
            move_slab_to_partial(slab->parent_cache, slab);
        }
    }
}