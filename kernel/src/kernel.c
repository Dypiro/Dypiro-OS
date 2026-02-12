#include <stdint.h>
#include <stddef.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "printf.h"
#include "kernel.h"
#include "keyboard.h"
#include "main.h"
//#include "io.h"
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

// Simple string-to-int conversion
int simple_atoi(char* str) {
    int res = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        if (str[i] < '0' || str[i] > '9') break; // Stop if not a digit
        res = res * 10 + str[i] - '0';
    }
    return res;
}

uint64_t ticks = 0;

void timer_handler_c() {
    ticks++;
    if (ticks == target_ticks) {
       printf("\ncount target reached!\n>");
    }
}

int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (*str1 == *str2) ? 1 : 0;
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

void shell_input(char c) {
    if (c == '\n') {
        shell_buffer[buffer_idx] = '\0'; // Null-terminate the string
        execute_command(shell_buffer);
        buffer_idx = 0;                  // Reset buffer
        printf("\n>");
    } 
    else if (c == '\b') {
        if (buffer_idx > 0) {
            buffer_idx--;
            printf("\b \b"); // Backspace, space (to clear), Backspace
        }
    } 
    else if (buffer_idx < MAX_COMMAND_LEN - 1) {
        shell_buffer[buffer_idx++] = c;
        printf("%c", c); // Echo the character back to the user
    }
}

void execute_command(char* input) {
    if (strcmp(input, "help")) {
        printf("\nAvailable commands: help, clear, ticks, sleep");
    } 
    else if (strcmp(input, "clear")) {
        // If you have a clear screen function, call it here
        printf("\033[2J\033[H"); // Standard ANSI clear (if supported)
    } 
    else if (strcmp(input, "ticks")) {
        printf("\nCurrent system ticks: %d", (int)ticks);
    } 
    // SLEEP COMMAND: expects "sleep <ms>"
    else if (strncmp(input, "count ", 6) == 0) { // Using your strncmp logic
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