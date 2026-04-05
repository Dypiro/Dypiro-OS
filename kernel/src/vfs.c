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