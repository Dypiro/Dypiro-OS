#include <stddef.h>
#include <stdint.h>
// 1. Tell the compiler "This struct exists" (Forward Declaration)
struct vfs_node;

// 2. Define the function pointer type using the struct name
typedef uint64_t (*vfs_read_t)(struct vfs_node* node, uint64_t offset, uint64_t size, uint8_t* buffer);

// 3. Define the actual struct using the typedef we just made
typedef struct vfs_node {
    char name[256];
    uint32_t type;
    uint64_t size;
    uint64_t inode;      
    void* private_data;  
    
    vfs_read_t read;     // Now the compiler knows exactly what this is!
    struct vfs_node* next; 
} vfs_node_t;

// 4. Function prototypes
void vfs_mount_tar(void* tar_address);
vfs_node_t* vfs_open(const char* name);