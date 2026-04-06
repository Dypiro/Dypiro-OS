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
uint64_t vfs_read(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buffer) {
    if (offset + size > node->size) {
        size = node->size - offset; // Don't read past the end of the file
    }

    // Copy from our internal storage (private_data) to the user's buffer
    memcpy(buffer, (uint8_t*)node->private_data + offset, size);
    return size;
}


uint64_t vfs_tar_write(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buffer) {
    if (offset + size > node->size) {
        size = node->size - offset; // Cannot expand a TAR entry!
    }
    memcpy((uint8_t*)node->private_data + offset, buffer, size);
    return size;
}

uint64_t vfs_ram_write(vfs_node_t* node, uint64_t offset, uint64_t size, uint8_t* buffer) {
    // Basic version: Still limited by initial kmalloc size.
    // Advanced version: You would krealloc(node->private_data) if offset+size > node->size
    if (offset + size > node->size) {
        size = node->size - offset; 
    }
    memcpy((uint8_t*)node->private_data + offset, buffer, size);
    return size;
}

void vfs_mount_tar(void* tar_address) {
    uint8_t* ptr = (uint8_t*)tar_address;

    while (memcmp(ptr, "\0\0", 2) != 0) {
        char typeflag = *(char*)(ptr + 156);
        
        vfs_node_t* node = kmalloc(sizeof(vfs_node_t));
        strncpy(node->name, (char*)ptr, 100);
        node->size = tar_parse_octal((char*)(ptr + 124));
        
        // CHECK THE TYPE
        if (typeflag == '5') {
            node->type = VFS_DIRECTORY;
            node->private_data = NULL; // Directories have no raw data
        } else {
            node->type = VFS_FILE;
            node->private_data = (ptr + 512);
        }

        node->read = vfs_read;
        node->write = vfs_tar_write;

        node->next = root_fs;
        root_fs = node;

        ptr += 512 + ((node->size + 511) & ~511);
    }
}

vfs_node_t* vfs_open(const char* name) {
    vfs_node_t* curr = root_fs;

    while (curr != NULL) {
        const char* vfs_name = curr->name;
        const char* search_name = name;

        // Skip the ./ prefix if it exists in VFS but not in user input
        if (vfs_name[0] == '.' && vfs_name[1] == '/' && search_name[0] != '.') {
            vfs_name += 2;
        }

        int result = strcmp(vfs_name, search_name);
        
        
        //printf("\nVFS_DEBUG: Comparing '%s' to '%s' | Result: %d\n", vfs_name, search_name, result);

        if (result == 0) {
            //printf("\nVFS_DEBUG: Match Found!\n");
            return curr;
        }

        curr = curr->next;
    }
    return NULL;
}

vfs_node_t* vfs_touch(const char* name, uint64_t initial_size) {
    vfs_node_t* new_node = kmalloc(sizeof(vfs_node_t));
    
    // BAD: new_node->name = name; 
    // (This just points to the shell's temporary command buffer!)

    // GOOD: Copy the string into the node's own internal buffer
    strncpy(new_node->name, name, 255);
    new_node->name[255] = '\0'; // Always null-terminate!

    new_node->type = VFS_FILE;
    new_node->size = initial_size;
    new_node->private_data = kmalloc(initial_size);
    memset(new_node->private_data, 0, initial_size);
    
    new_node->read = vfs_read;
    new_node->write = vfs_ram_write;
    
    new_node->next = root_fs;
    root_fs = new_node;
    
    return new_node;
}

void vfs_ls(const char* filter) {
    vfs_node_t* curr = root_fs;
    char last_dir[256] = {0}; // Track what we just printed

    while (curr != NULL) {
        const char* name = curr->name;
        if (name[0] == '.' && name[1] == '/') name += 2;

        // 1. Filter logic (same as before)
        if (filter && strlen(filter) > 0) {
            if (strncmp(name, filter, strlen(filter)) != 0) {
                curr = curr->next;
                continue;
            }
            name += strlen(filter);
            if (name[0] == '/') name++;
        }

        // 2. Directory vs File logic
        char* next_slash = strchr(name, '/');
        if (next_slash == NULL) {
            // It's a file: Print it!
            printf("  %s\n", name);
        } else {
            // It's a directory: Only print if it's not the same as the last one
            int dir_len = next_slash - name;
            if (strncmp(last_dir, name, dir_len) != 0 || last_dir[dir_len] != '\0') {
                printf("  %.*s/\n", dir_len, name);
                
                // Update last_dir
                strncpy(last_dir, name, dir_len);
                last_dir[dir_len] = '\0';
            }
        }
        curr = curr->next;
    }
}