// Copyright 2012 Rui Ueyama. Released under the MIT license.

// This file contains routines for dealing with the buffer type.

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
        // argument 'fmt'.
        va_start(args, fmt);

        // printf-family functions that have a name preceded by a 'v' are called
        // with a va_list instead of the arguments themselves; they internally
        // call the va_arg() macro, so we don't need to call it manually.
        // printf-family functions preceded by a 's' print to a string, in 
        // this case the string is a pointer to the next position in the buffer 
        // that we can write to; the code utilizes pointer arithmetic to calculate
        // exactly where that is.
        // printf-family functions that have a name preceded by a 'n' print only
        // a certain number of characters, in this case avail chars.
        // This call returns the number of bytes written.
        int written = vsnprintf(b->body + b->len, avail, fmt, args);

        // va_end cleans up after va_start()
        va_end(args);

        // If the number of bytes written is larger than the number of bytes 
        // that were available, then we need to reallocate and continue.
        if (avail <= written) {
            realloc_body(b);
            continue;
        }

        // Update the length.
        b->len += written;
        return;
    }
}

char *vformat(char *fmt, va_list ap) {
    Buffer *b = make_buffer();
    va_list aq;
    for (;;) {
        int avail = b->nalloc - b->len;
        va_copy(aq, ap);
        int written = vsnprintf(b->body + b->len, avail, fmt, aq);
        va_end(aq);
        if (avail <= written) {
            realloc_body(b);
            continue;
        }
        b->len += written;
        return buf_body(b);
    }
}

char *format(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *r = vformat(fmt, ap);
    va_end(ap);
    return r;
}

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

static void print(Buffer *b, char c) {
    char *q = quote(c);
    if (q) {
        buf_printf(b, "%s", q);
    } else if (isprint(c)) {
        buf_printf(b, "%c", c);
    } else {
        buf_printf(b, "\\x%02x", c);
    }
}

char *quote_cstring(char *p) {
    Buffer *b = make_buffer();
    while (*p)
        print(b, *p++);
    return buf_body(b);
}

char *quote_cstring_len(char *p, int len) {
    Buffer *b = make_buffer();
    for (int i = 0; i < len; i++)
        print(b, p[i]);
    return buf_body(b);
}

char *quote_char(char c) {
    if (c == '\\') return "\\\\";
    if (c == '\'') return "\\'";
    return format("%c", c);
}
