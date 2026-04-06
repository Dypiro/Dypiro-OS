#include <stddef.h>
#include <stdint.h>
// 1. Tell the compiler "This struct exists" (Forward Declaration)
struct vfs_node;

// 2. Define the function pointer type using the struct name
typedef uint64_t (*vfs_read_t)(struct vfs_node* node, uint64_t offset, uint64_t size, uint8_t* buffer);
typedef uint64_t (*vfs_write_t)(struct vfs_node* node, uint64_t offset, uint64_t size, const uint8_t* buffer);

// 3. Define the actual struct using the typedef we just made
typedef struct vfs_node {
    char name[256];
    uint32_t type;
    uint64_t size;
    uint64_t inode;      
    void* private_data;  
    
    uint64_t (*read)(struct vfs_node*, uint64_t, uint64_t, uint8_t*);
    uint64_t (*write)(struct vfs_node*, uint64_t, uint64_t, uint8_t*);
    struct vfs_node* next; 
} vfs_node_t;

// 4. Function prototypes
void vfs_mount_tar(void* tar_address);
vfs_node_t* vfs_open(const char* name);
vfs_node_t* vfs_touch(const char* name, uint64_t initial_size);
void vfs_ls();