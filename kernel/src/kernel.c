#include <stdint.h>
#include <stddef.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "printf.h"
#include "kernel.h"
#include "keyboard.h"
#include "timer.h"
#include "main.h"
#define pass (void)0

int scancode;
int element = 0;
char msg[50] = {};
extern uint8_t read_port(uint16_t port);
extern void write_port(uint16_t port, uint8_t value);

// Track key state for repeat handling
static uint8_t last_scancode = 0;
static uint8_t key_repeat_started = 0;

void reset_keyboard(){
    scancode = 129;
    memset(msg,0,sizeof(msg));
    element = 0;
    last_scancode = 0;
    key_repeat_started = 0;
    char msg[50] = {};
}

void keyboard(){
    while (1){
        uint8_t scancode = read_port(0x60);
        
        // Key release event (scancode > 128)
        if (scancode > 128){
            last_scancode = 0;
            key_repeat_started = 0;
            continue;
        }
        
        // New key pressed (different from previous)
        if (scancode != last_scancode){
            last_scancode = scancode;
            key_repeat_started = 0;
            
            char character = keyboard_map[scancode];
            printf("%c", character);
            
            // Enter key - break out of input loop
            if (scancode == 28){
                break;
            }
            else{
                msg[element] = character;
                element++;
            }
            
            // Wait before allowing key repeat to start
            delay(500);
        }
        // Key is being held down - handle repeat
        else if (last_scancode != 0){
            char character = keyboard_map[scancode];
            printf("%c", character);
            msg[element] = character;
            element++;
            
            // Delay between repeat characters
            delay(100);
        }
    }
}

int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (*str1 == *str2) ? 1 : 0;
}

void kmain(){
    while (1){
        //keyboard manoovering
        printf(">");
        keyboard();
        delay(400);
        if (strcmp(msg, "test")){
            printf("haha yez\n");
        }
        else{
            printf("No such command as: %s\n", msg);
        }
        reset_keyboard();
    }
}