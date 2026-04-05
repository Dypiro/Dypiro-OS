#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "kernel.h"
#include "printf.h"
#include "mem.h"
#include "vfs.h"

#define VFS_FILE 0x01
#define VFS_DIRECTORY 0x02

struct vfs_node;

vfs_node_t* root_fs = NULL; // Our global root

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for ( ; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

// Helper to parse the weird TAR octal strings
uint64_t tar_parse_octal(const char *in) {
    uint64_t out = 0;
    while (*in >= '0' && *in <= '7') {
        out = (out << 3) + (*in - '0');
        in++;
    }
    return out;
}

// The actual read implementation for TAR files
uint64_t tar_read(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buffer) {
    if (offset >= node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;

    // In a TAR, node->private_data points to the start of the file data in RAM
    memcpy(buffer, (uint8_t*)node->private_data + offset, size);
    return size;
}


uint64_t tar_write(vfs_node_t* node, uint64_t offset, uint64_t size, const uint8_t* buffer) {
    if (offset >= node->size) return 0; // Out of bounds
    
    // Don't allow writing past the end of the existing file
    if (offset + size > node->size) {
        size = node->size - offset;
    }

    // private_data points to the file's start in the TAR RAM blob
    memcpy((uint8_t*)node->private_data + offset, buffer, size);
    
    return size;
}

uint64_t ram_write(vfs_node_t* node, uint64_t offset, uint64_t size, const uint8_t* buffer) {
    // For a simple version, we assume the buffer was pre-allocated
    // In a pro VFS, you'd realloc() node->private_data if (offset + size > node->size)
    
    if (offset + size > node->size) return 0; // Simple guard for now

    memcpy((uint8_t*)node->private_data + offset, buffer, size);
    return size;
}

void vfs_mount_tar(void* tar_address) {
    uint8_t* ptr = (uint8_t*)tar_address;

    while (memcmp(ptr, "\0\0", 2) != 0) { // TAR ends with null blocks
        vfs_node_t* node = kmalloc(sizeof(vfs_node_t));
        
        // Populate node from TAR header
        memcpy(node->name, ptr, 100);
        node->size = tar_parse_octal((char*)(ptr + 124));
        node->type = VFS_FILE;
        node->private_data = (ptr + 512); // Data starts after header
        node->read = tar_read;            // Assign our function pointer
        node->write = tar_write;          // Assign the writer

        // Link into our global list (simple flat VFS for now)
        node->next = root_fs;
        root_fs = node;

        // Move to next header: 512 (header) + size aligned to 512
        ptr += 512 + ((node->size + 511) & ~511);
    }
}

vfs_node_t* vfs_open(const char* name) {
    vfs_node_t* curr = root_fs;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr;
        curr = curr->next;
    }
    return NULL;
}

vfs_node_t* vfs_touch(const char* name, uint64_t initial_size) {
    // 1. Create the VFS header
    vfs_node_t* new_node = kmalloc(sizeof(vfs_node_t));
    
    // 2. Setup metadata
    strncpy(new_node->name, name, 255);
    new_node->type = VFS_FILE;
    new_node->size = initial_size;
    
    // 3. Allocate actual storage in RAM for this file
    new_node->private_data = kmalloc(initial_size);
    memset(new_node->private_data, 0, initial_size); // Clear it
    
    // 4. Assign functions (Use the RAM-friendly versions)
    new_node->read = tar_read;   // tar_read actually works for RAM too!
    new_node->write = ram_write; 
    
    // 5. Link it into the list so vfs_open can find it later
    new_node->next = root_fs;
    root_fs = new_node;
    
    return new_node;
}