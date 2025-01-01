/**
 * Copyright (c) Turrnut Open Source Organization
 * Under the GPL v3 License
 * See COPYING for information on how you can use this file
 * 
 * keyboard.c
 */

#include "keyboard.h"
#include "os.h"
#include "screen.h"
#include "console.h"

#define UP_ARROW    -1
#define DOWN_ARROW  -2
#define LEFT_ARROW  -3
#define RIGHT_ARROW -4

char input_buffer[INPUT_BUFFER_SIZE];
size_t input_len = 0;

volatile uint8_t key_buffer[KEY_BUFFER_SIZE];
volatile size_t buffer_head = 0;
volatile size_t buffer_tail = 0;

// Command history
#define COMMAND_HISTORY_SIZE 5
extern char command_history[COMMAND_HISTORY_SIZE][INPUT_BUFFER_SIZE];
extern int command_history_index;
extern int current_history_index;

// Add a scancode to the buffer
void buffer_add(uint8_t scancode) {
    size_t next_head = (buffer_head + 1) % KEY_BUFFER_SIZE;
    if (next_head != buffer_tail) { // Don't overwrite
        key_buffer[buffer_head] = scancode;
        buffer_head = next_head;
    }
}

// Get a scancode from the buffer
int buffer_get(uint8_t* scancode) {
    if (buffer_head == buffer_tail) {
        return 0; // Buffer is empty
    }
    *scancode = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEY_BUFFER_SIZE;
    return 1;
}

char scancode_to_ascii(uint8_t scancode) {
    static const uint8_t scancode_map[128] = {
        0,  0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0,
        // Rest is zeros for now
    };

    if (scancode == 0x48) return UP_ARROW;     // Up arrow
    if (scancode == 0x50) return DOWN_ARROW;   // Down arrow
    if (scancode == 0x4B) return LEFT_ARROW;   // Left arrow
    if (scancode == 0x4D) return RIGHT_ARROW;  // Right arrow

    if (scancode < 128) {
        return scancode_map[scancode];
    }
    return 0; // Invalid scancode
}

void handle_keypress(uint8_t scancode) {
    delay_ms(120); // Enforce delay between keypresses

    if (scancode & 0x80) {
        return; // Ignore key releases
    }

    char ascii = scancode_to_ascii(scancode);
    if (!ascii) {
        return; // Skip invalid keys
    }

    if (ascii == '\b') { // Handle backspace
        if (input_len > 0) {
            input_len--;
            input_buffer[input_len] = '\0';
            if (curs_col == 0 && curs_row > 0) {
                curs_row--;
                curs_col = NUM_COLS - 1;
            } else {
                curs_col--;
            }
            vga_buffer[curs_row * NUM_COLS + curs_col] = (struct Char){' ', default_color};
            update_cursor();
        }
    } else if (ascii == UP_ARROW) {
        col = 0;
        if (current_history_index < 0) {
            current_history_index = command_history_index - 1; // Start from the last command
        } else if (current_history_index > 0) {
            current_history_index--; // Go to the previous command
        }

        if (current_history_index >= 0) {
            // Load the command from history
            strncpy(input_buffer, command_history[current_history_index], INPUT_BUFFER_SIZE);
            input_len = strlen(input_buffer);
        } else {
            input_buffer[0] = '\0'; // No command to load
            input_len = 0;
        }

        // Clear the current line by overwriting it
        for (size_t i = 0; i < NUM_COLS; i++) {
            vga_buffer[curs_row * NUM_COLS + i] = (struct Char){' ', default_color};
        }

        // Print the new command and move the cursor to the end
        move_cursor_back();
        print(input_buffer);
        curs_col = input_len; // Set cursor to the end of the command
        update_cursor();
    } else if (ascii == DOWN_ARROW) {
        col = 0;
        if (current_history_index < command_history_index - 1) {
            current_history_index++; // Go to the next command
            strncpy(input_buffer, command_history[current_history_index], INPUT_BUFFER_SIZE);
            input_len = strlen(input_buffer);
        } else {
            current_history_index = -1; // Clear input if at the latest
            input_buffer[0] = '\0';
            input_len = 0;
        }

        // Clear the current line by overwriting it
        for (size_t i = 0; i < NUM_COLS; i++) {
            vga_buffer[curs_row * NUM_COLS + i] = (struct Char){' ', default_color};
        }

        // Print the new command and move the cursor to the end
        move_cursor_back();
        print(input_buffer);
        curs_col = input_len; // Set cursor to the end of the command
        update_cursor();
    } else if (ascii == LEFT_ARROW) {
        if (curs_col > 0) {
            curs_col--;
            update_cursor();
        }
    } else if (ascii == RIGHT_ARROW) {
        if (curs_col < input_len) {
            curs_col++;
            update_cursor();
        }
    } else if (ascii == '\n') { // Handle newline
        input_buffer[input_len] = '\0';

        // Save the command to history
        if (input_len > 0) {
            strncpy(command_history[command_history_index], input_buffer, INPUT_BUFFER_SIZE);
            command_history_index = (command_history_index + 1) % COMMAND_HISTORY_SIZE;
        }

        println(""); // Move to a new line
        process_command(input_buffer);
        input_len = 0;
        curs_row++;
        curs_col = 0;
        if (curs_row >= NUM_ROWS) {
            scroll_screen();
            curs_row = NUM_ROWS - 1;
        }
        update_cursor();
    } else { // Handle regular characters
        if (input_len < INPUT_BUFFER_SIZE - 1) {
            input_buffer[input_len++] = ascii;
        }
        vga_buffer[curs_row * NUM_COLS + curs_col] = (struct Char){ascii, default_color};
        curs_col++;
        if (curs_col >= NUM_COLS) {
            curs_col = 0;
            curs_row++;
        }
        if (curs_row >= NUM_ROWS) {
            scroll_screen();
            curs_row = NUM_ROWS - 1;
        }
        update_cursor();
    }
}
