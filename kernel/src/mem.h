#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_NX       (1ULL << 63) // No-Execute bit

// This mask helps us clear the flags to get just the physical address
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define PML4_IDX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_IDX(addr) (((addr) >> 30) & 0x1FF)
#define PD_IDX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_IDX(addr)   (((addr) >> 12) & 0x1FF) 

#define PTE_PRESENT  (1ULL << 0)   // Bit 0: Is the page actually in RAM?
#define PTE_WRITABLE (1ULL << 1)   // Bit 1: Can we write to it?
#define PTE_USER     (1ULL << 2)   // Bit 2: Can user-mode code touch it?
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL // Used to strip flags and get address

void *memset(void *s, int c, size_t n);

//We need to know where the current active page table is.
//On x86_64, the physical address of the PML4 is stored in the CR3 register.
static inline uint64_t read_cr3(void) {
    uint64_t value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

extern uint64_t* kernel_pml4;

//uint64_t phys_pml4;
typedef uint64_t pt_entry;

void pmm_init();
void pmm_set_page();
void pmm_free_page();
void pmm_init_free_regions();
uint64_t pmm_alloc();
void lock_bitmap();
void limine_check();
void vmm_init();
void vmm_map(uint64_t* pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
pt_entry* vmm_get_pte(uint64_t* pml4, uint64_t virt_addr, bool allocate);
