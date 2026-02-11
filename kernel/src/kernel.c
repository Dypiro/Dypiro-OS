#include <stdint.h>
#include <stddef.h>
#include <flanterm/flanterm.h>
#include <flanterm/backends/fb.h>
#include "printf.h"
#include "kernel.h"
#include "keyboard.h"
#include "timer.h"
#include "main.h"
//#include "io.h"
#define pass (void)0

static int shift_pressed = 0;
int scancode;
int element = 0;
char msg[50] = {};
extern uint8_t read_port(uint16_t port);
extern void write_port(uint16_t port, uint8_t value);

void keyboard_handler_c() {
    uint8_t scancode = read_port(0x60); // Read the key
    
    // If the top bit is set, it's a "key released" event (ignore for now)
    if (!(scancode & 0x80)) {
        if (keyboard_map[scancode]) {
            printf("%c", keyboard_map[scancode]);
        }
    }
    // Send EOI (End Of Interrupt) to the Master PIC
    write_port(0x20, 0x20); 
}
// Track key state for repeat handling
static uint8_t last_scancode = 0;
static uint8_t key_repeat_started = 0;

void reset_keyboard(){ //WILL BE REWORKED
    scancode = 129;
    memset(msg,0,sizeof(msg));
    element = 0;
    last_scancode = 0;
    key_repeat_started = 0;
}

void keyboard(){ //THIS IS OLD AND WILL BE REWORKED
    int cursor = 0;   // cursor position inside msg

    while (1){
        uint8_t scancode = read_port(0x60);

        if (scancode > 128) { //key release
            uint8_t released = scancode - 128;

            if (released == 42 || released == 54) // LShift or RShift
                shift_pressed = 0;

            last_scancode = 0;
            continue;
        }
        //SHIFT
        if (scancode == 42 || scancode == 54) { // LShift or RShift
            shift_pressed = 1;
            last_scancode = scancode;
            delay(100);
            continue;
        }

        if (scancode != last_scancode){
            last_scancode = scancode;

            //ENTER
            if (scancode == 28){ //Enter
                printf("\n");
                break;
            }

            //BACKSPACE 
            if (scancode == 14){ //Backspace
                if (cursor > 0){
                    cursor--;
                    element--;
                    msg[cursor] = 0;
                    printf("\b \b");   // erase from screen
                }
                delay(100);
                continue;
            }

            //NORMAL CHARACTER
            char character;

            // pick map based on shift
            if (shift_pressed)
                character = keyboard_map_shift[scancode];
            else
                character = keyboard_map[scancode];


            if (character >= 'A' && character <= 'Z') {
                if (shift_pressed == 0)
                    character = character + 32;      // a -> A
            }


            if (character && element < sizeof(msg)-1){
                // insert at cursor (not only at end)
                for (int i = element; i > cursor; i--){
                    msg[i] = msg[i-1];
                }
                msg[cursor] = character;
                element++;
                cursor++;

                printf("%c", character);
            }

            delay(200);
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
        /*//keyboard manoovering
        //printf(">");
        //keyboard();
        delay(400);
        if (strcmp(msg, "ver")){
            printf("Dypiro-OS 2.0\n");
        }
        else if (strcmp(msg, "help")){
            printf("-help\n-ver\n-clear\n-echo\n-rng\n-reboot\n-halt\n");
        }
        else if (strcmp(msg, "rng")){
            printf("%i\n", random(0, 100));
        }
        else if (strcmp(msg, "clear")){
            for (int i = 0; i < 60; i++)
                printf("\n");
        }
        else if (strncmp(msg, "echo ", 5) == 0){
            printf("%s\n", msg + 5);
        }
        else if (strcmp(msg, "reboot")){
           write_port(0x64, 0xFE); // classic PC reboot
        }
        else if (strcmp(msg, "halt")){
            printf("This computer is safe to turn off manually.\nSYSTEM HALTED!!!");
            asm("hlt"); // kernel becomes disfunctional and safe to power off
        }
        else{
            printf("No such command as: %s\n", msg);
        }
        //reset_keyboard();*/
        ;;
    }
}