// Copyright 2012 Rui Ueyama. Released under the MIT license.

// This file contains routines for dealing with the buffer type. An interesting 
// design decision to note in 8cc is that there's no function to destroy a 
// buffer, meaning that all memory simply gets allocated without ever being freed.
// This is acceptable because compilers do not generally run long enough to
// use enough memory to cause problems.

#include <ctype.h>
// stdarg is a file in the C standard library that allows functions to accept an
// indefinite number of arguments.
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define INIT_SIZE 8

// This function creates a buffer.
Buffer *make_buffer() {
    Buffer *r = malloc(sizeof(Buffer));

    // We allocate memory for the members of the struct.
    // malloc() allocates memory on the heap without initializing that memory.
    r->body = malloc(INIT_SIZE);

    // nalloc is a member that keeps track of the size of the buffer.
    r->nalloc = INIT_SIZE;

    // len tracks the 'length' of the buffer, i.e. how many bytes have been used.
    r->len = 0;
    return r;
}

// This function resizes the buffer to make it bigger.
static void realloc_body(Buffer *b) {
    // The new size is twice as large as the previous size.
    int newsize = b->nalloc * 2;
    char *body = malloc(newsize);

    // memcpy() copies memory. Here, we copy memory from the old body to the
    // new one.
    memcpy(body, b->body, b->len);

    // The other members stay the same.
    b->body = body;
    b->nalloc = newsize;
}

// This function just returns the body of the buffer.
char *buf_body(Buffer *b) {
    return b->body;
}

// This function just returns the used length of the buffer.
int buf_len(Buffer *b) {
    return b->len;
}

// This function writes a single character to the buffer.
void buf_write(Buffer *b, char c) {
    // If the length of the buffer is about to exceed the size of the buffer that
    // has been allocated, then we need to reallocate the buffer to give it more
    // memory.
    if (b->nalloc == (b->len + 1))
        realloc_body(b);

    // Put the character in the next available position.
    b->body[b->len++] = c;
}

// Append an array of characters to the buffer.
void buf_append(Buffer *b, char *s, int len) {
    // Write each character to the buffer individually.
    for (int i = 0; i < len; i++)
        buf_write(b, s[i]);
}

// Print to the buffer. Note that this function takes a format string and a 
// variable number of arguments, just like printf().
void buf_printf(Buffer *b, char *fmt, ...) {
    // va_list contains the variable args to the function.
    va_list args;
    for (;;) {
        // Calculate how many bytes are available.
        int avail = b->nalloc - b->len;

        // Retrieve arguments from the variadic arguments following the named
        // argument 'fmt'. This places the arguments into the variable va_list
        va_start(args, fmt);

        // printf-family functions that have a name preceded by a 'v' are called
        // with a va_list instead of the arguments themselves; they internally
        // call the va_arg() macro, so we don't need to call it manually.
        // printf-family functions preceded by a 's' print to a string, in 
        // this case the 'string' is a pointer to the next position in the buffer 
        // that we can write to; the code utilizes pointer arithmetic to calculate
        // exactly where that is.
        // printf-family functions that have a name preceded by a 'n' print only
        // a certain number of characters, in this case 'avail' chars.
        // This call returns the number of bytes written, or the number of bytes 
        // that would have been written assuming that there was enough space.
        int written = vsnprintf(b->body + b->len, avail, fmt, args);

        // va_end cleans up after va_start()
        va_end(args);

        // If value of 'written' is larger than the number of available bytes, 
        // then we know the write didn't actually happen, and we need to 
        // reallocate the buffer with more memory and try again.
        if (avail <= written) {
            realloc_body(b);
            continue;
        }

        // Update the length.
        b->len += written;
        return;
    }
}

// For some reason, the body of the function below (format()) has been spun out 
// into its own function. This function takes a format string and a list of 
// variadic arguments, as a va_list, and returns the corresponding string.
char *vformat(char *fmt, va_list ap) {
    // Create a buffer.
    Buffer *b = make_buffer();

    // Variable to store a copy of the arguments; we don't want to mutate them
    // within this function.
    va_list aq;
    for (;;) {
        // Calculare the number of available bytes.
        int avail = b->nalloc - b->len;

        // Copy the argument list.
        va_copy(aq, ap);

        // Use vsnprintf to perform the actual format operation. See above for 
        // an explanation of this function.
        int written = vsnprintf(b->body + b->len, avail, fmt, aq);
        
        // Clean up the copy
        va_end(aq);

        // If we didn't have enough bytes to actually write to the buffer, we 
        // reallocate and try again.
        if (avail <= written) {
            realloc_body(b);
            continue;
        }

        // Update the length and return the body of the buffer.
        b->len += written;
        return buf_body(b);
    }
}

// Format a string.
char *format(char *fmt, ...) {
    // Set up the argument list.
    va_list ap;
    va_start(ap, fmt);

    // use the vformat() function to perform the actual formatting.
    char *r = vformat(fmt, ap);

    // Clean up and return.
    va_end(ap);
    return r;
}

// Convert an unescaped character to an escaped one.
static char *quote(char c) {
    switch (c) {
    case '"': return "\\\"";
    case '\\': return "\\\\";
    case '\b': return "\\b";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    }
    return NULL;
}

// Print a character to a buffer.
static void print(Buffer *b, char c) {
    // Escape the character
    char *q = quote(c);
    
    // If the character needed to be escaped, then quote() would have returned
    // a string...
    if (q) {
        
        // ..so we need to use "%s" to print it like one. 
        buf_printf(b, "%s", q);
    
    // isprint() checks if a character is printable.
    } else if (isprint(c)) {
        // Print c like a character, because it is one.
        buf_printf(b, "%c", c);
    } else {
        // Otherwise, the character isn't printable, so we print out the character
        // code, prefixed by "\x".
        buf_printf(b, "\\x%02x", c);
    }
}

// This function uses a buffer to get the escaped form of a cstring (an array 
// of characters suffixed by \0). 
char *quote_cstring(char *p) {
    // Create a buffer to store the escaped string.
    Buffer *b = make_buffer();

    // Loop through the string and print characters to the buffer.
    while (*p)
        print(b, *p++);

    // Return the content of the buffer.
    return buf_body(b);
}

// Does the same as the above function, but also takes a length.
char *quote_cstring_len(char *p, int len) {
    // Create a buffer.
    Buffer *b = make_buffer();

    // Consume 'len' chars and print them to the buffer.
    for (int i = 0; i < len; i++)
        print(b, p[i]);

    // Return the content of the buffer.
    return buf_body(b);
}

// Converts \ and ' to their escaped equivalents.
char *quote_char(char c) {
    if (c == '\\') return "\\\\";
    if (c == '\'') return "\\'";
    return format("%c", c);
}
