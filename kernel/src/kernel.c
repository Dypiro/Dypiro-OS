#include <stdint.h>
#include <stddef.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "printf.h"
#include "kernel.h"
#include "keyboard.h"
#include "mem.h"
#include "vfs.h"
#define pass (void)0
#define MAX_COMMAND_LEN 128

char shell_buffer[MAX_COMMAND_LEN];
int buffer_idx = 0;

static int shift_pressed = 0;
int scancode;
int element = 0;
uint64_t target_ticks = 0;
char msg[50] = {};
int cursor = 0;   // cursor position inside msg
extern uint8_t read_port(uint16_t port);
extern void write_port(uint16_t port, uint8_t value);

void keyboard_handler_c() {
    uint8_t scancode = read_port(0x60);
    if (!(scancode & 0x80)) { // Key press
        char c = keyboard_map[scancode];
        if (c) {
            shell_input(c);
        }
    }
}

uint64_t ticks = 0;

void timer_handler_c() {
    ticks++;
    if (ticks == target_ticks) {
       printf("\ncount target reached!\n>");
    }
}


// Simple string-to-int conversion
int simple_atoi(char* str) {
    int res = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break; // Stop if not a digit
        res = res * 10 + str[i] - '0';
    }
    return res;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char *cs, const char *ct, size_t count)
{
	unsigned char c1, c2;

	while (count) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
		count--;
	}
	return 0;
}

// We'll keep a pointer instead of an array
char* cmd = NULL; 
int* size = NULL;
void shell_input(char c) {
    // 1. If we don't have a buffer yet, grab one from the Slab
    if (cmd == NULL) {
        cmd = (char*)kmalloc(MAX_COMMAND_LEN);
        if (!cmd) return; // Out of memory!
    }
    if (size == NULL){
        size = (int*)kmalloc(10240);
    }

    if (c == '\n') {
        cmd[buffer_idx] = '\0';
        
        // Pass the heap pointer directly to the executor
        execute_command(cmd);

        // 2. The command is done. Free the memory and reset.
        kfree(cmd);
        cmd = NULL; 
        buffer_idx = 0;
        
        printf("\n>");
    } 
    else if (c == '\b') {
        if (buffer_idx > 0) {
            buffer_idx--;
            printf("\b \b");
        }
    } 
    else if (buffer_idx < MAX_COMMAND_LEN - 1) {
        cmd[buffer_idx++] = c;
        printf("%c", c);
    }
}

void execute_command(char* input) {
    if (strcmp(input, "help") == 0) {
        printf("\nAvailable commands: help, clear, count, echo, ticks, ls, read, write [file] [content], touch, size (set size for creating a file)");
    } 
    else if (strcmp(input, "clear") == 0) {
        // If you have a clear screen function, call it here
        printf("\033[2J\033[H"); // Standard ANSI clear (if supported)
    } 
    else if (strcmp(input, "ticks") == 0) {
        printf("\nCurrent system ticks: %d", (int)ticks);
    } 

    else if (strcmp(input, "ls") == 0) {
        printf("\n");
        vfs_ls();
    } 
    else if (strncmp(input, "read ", 5) == 0) {
        char* filename = input + 5;
        vfs_node_t* node = vfs_open(filename);
        if (node) {
            // Temporarily allocate a buffer to read into
            char* buf = kmalloc(node->size + 1);
            node->read(node, 0, node->size, (uint8_t*)buf);
            buf[node->size] = '\0'; 
            printf("\n%s\n", buf);
            kfree(buf);
        } else {
            printf("\nFile not found.\n");
        }
    }
    else if (strncmp(input, "touch ", 6) == 0) {
        char* filename = input + 6;
        vfs_touch(filename, *size);
        printf("\nCreated file %s\n",filename);
    }
    else if (strncmp(input, "write ", 6) == 0) {
        printf("\n");
        char* filename = input + 6;
        char* content = NULL;

        // Find the space between <filename> and <content>
        for (int i = 0; filename[i] != '\0'; i++) {
            if (filename[i] == ' ') {
                filename[i] = '\0';      // Split the string
                content = &filename[i+1]; // Content starts after the space
                break;
            }
        }

        if (content == NULL) {
            printf("Usage: write <filename> <text>\n");
            return;
        }

        vfs_node_t* node = vfs_open(filename);
        if (node) {
            uint64_t len = strlen(content);
            uint64_t written = node->write(node, 0, len, (uint8_t*)content);
            
            if (written < len) {
                printf("Warning: Only wrote %d/%d bytes (file too small?)\n", written, len);
            } else {
                printf("Successfully wrote to %s\n", filename);
            }
        } else {
            printf("Error: File '%s' not found.\n", filename);
        }
    } 
    // SLEEP COMMAND: expects "sleep <ms>"
    else if (strncmp(input, "count ", 6) == 0) {
        int ms = simple_atoi(input + 6);    // Skip the "sleep " part
        if (ms > 0) {
            target_ticks = ticks + ms; // Assuming 1000Hz (1ms per tick)
            printf("\nCounting to %d ticks...", ms);
        }
    }
    // ECHO COMMAND: expects "echo <message>"
    else if (strncmp(input, "echo ", 5) == 0) {
        printf("\n%s", input + 5); // Jump 5 chars ahead to skip "echo "
    }

    else if (strncmp(input, "size ", 5) == 0) {
        if (simple_atoi(input + 5) <= 10240){
            *size = simple_atoi(input + 5); 
        }
        else{
            kfree(size);
            size = (int*)kmalloc(10240); //prevent any overflows just in case
            printf("\nCurrent size = %i\nCannot set size beyond 10240 bytes");
        }

    }
    else if (input[0] == '\0') {
        // Do nothing for empty enter
    }
    else {
        printf("\nUnknown command: %s", input);
    }
}



// random generator
size_t seed = 1;
const size_t a = 2001;
const size_t c = 1 << 30;
const size_t m = (1 << 63) - 1;

int random(int min, int max) {
	int range = max - min;
	seed = (a * seed + c) % m;
	return min + (int)(seed % range);
}

void kmain(){
    while (1){
        ;;
    }
}