// Copyright 2014 Rui Ueyama. Released under the MIT license.

/*
 * This file provides character input stream for C source code.
 * An input stream is either backed by stdio's FILE * or
 * backed by a string.
 * The following input processing is done at this stage.
 *
 * - C11 5.1.1.2p1: "\r\n" or "\r" are canonicalized to "\n".
 * - C11 5.1.1.2p2: A sequence of backslash and newline is removed.
 * - EOF not immediately following a newline is converted to
 *   a sequence of newline and EOF. (The C spec requires source
 *   files end in a newline character (5.1.1.2p2). Thus, if all
 *   source files are comforming, this step wouldn't be needed.)
 *
 * Trigraphs are not supported by design.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

// The sys/stat.h header defines the structure of the data returned by the stat()
// family of functions. These functions are used to get information about a file.
#include <sys/stat.h>

#include <sys/types.h>
#include <unistd.h>
#include "8cc.h"

static Vector *files = &EMPTY_VECTOR;
static Vector *stashed = &EMPTY_VECTOR;

// Convert the C standard type 'FILE' to the 8cc type 'File'. FILE used to be a
// macro, before typedefs were added to C, which is why it was given the 
// all-caps name. Note the all caps, it will be important to make sense of
// the rest of the comments here.
File *make_file(FILE *file, char *name) {
    // Allocate memory for the File struct.
    File *r = calloc(1, sizeof(File));

    // an 8cc File can be file-backed or string backed. Here, we set the file
    // member for a FILE-backed File. :^)
    r->file = file;
    r->name = name;
    r->line = 1;
    r->column = 1;

    // Create a stat struct so we can get file information.
    struct stat st;

    // fileno() gets the file descriptor associated with a given FILE*. The file
    // descriptor is a number that is used by the operating system to keep track
    // of files. We then pass this file descriptor to fstat(), which gets
    // imformation about the file and writes it to the struct st.
    if (fstat(fileno(file), &st) == -1)
        error("fstat failed: %s", strerror(errno));

    // st_mtime stores the time that the data in the file was last modified.
    r->mtime = st.st_mtime;
    return r;
}

// Create a file from a string.
File *make_file_string(char *s) {
    // Allocate memory for the struct.
    File *r = calloc(1, sizeof(File));
    r->line = 1;
    r->column = 1;

    // the member 'p' on a File is the 
    r->p = s;
    return r;
}

// Close the file handle of a File f.
static void close_file(File *f) {
    // If our File is backed by a FILE, close it.
    if (f->file)
        fclose(f->file);
}

// Read a character from a FILE-backed File.
static int readc_file(File *f) {
    // getc() reads the next character from a file.
    int c = getc(f->file);

    // If the current character is EOF, we check to see what the last character
    // was. If it wasn't an EOF or a newline, we need to return a newline, for
    // the reason specified in the comment at the top of this file: because
    // the C standard requires it. This comes from UNIX, which specified that a
    // 'line' is newline-terminated. Because we don't actually want to give an
    // error if our file is not newline-terminated, we have to hack up our file
    // reading code like this.
    if (c == EOF) {
        c = (f->last == '\n' || f->last == EOF) ? EOF : '\n';
    
    // We also need to handle carriage returns, which need to be canonicalized as
    // newlines.
    } else if (c == '\r') {
        // The way we do this is via lookahead...
        int c2 = getc(f->file);
        
        // If the next character is a newline, then we can take it from the stream,
        // and replace the \r\n sequence with a single \n. However, if the lookahead
        // character is not a newline, we have to put it back into the stream using
        // the ungetc() function. Another example of C's silliness.
        if (c2 != '\n')
            ungetc(c2, f->file);
        c = '\n';
    }
    // Update the character that was last read.
    f->last = c;
    return c;
}

// Read a character from a File backed by a string.
static int readc_string(File *f) {
    int c;
    // If the end of the string has been reached, we need to check that the 
    // file is newline-terminated.
    if (*f->p == '\0') {
        c = (f->last == '\n' || f->last == EOF) ? EOF : '\n';

    // As before, we also need to canonicalize carriage returns to newlines.
    } else if (*f->p == '\r') {
        f->p++;
        if (*f->p == '\n')
            f->p++;
        c = '\n';
    } else {
        // Increment the character pointer.
        c = *f->p++;
    }
    f->last = c;
    return c;
}

// Get a character.
static int get() {
    // Get the last file.
    File *f = vec_tail(files);
    int c;

    // First check the push-back buffer. The push-back buffer contains characters
    // that have been 'unread'.
    if (f->buflen > 0) {
        c = f->buf[--f->buflen];
    } 
    
    // Next, check if the File is backed by a FILE.
    else if (f->file) {
        c = readc_file(f);
    } 
    
    // Otherwise, the File must be backed by a string, so we read from that.
    else {
        c = readc_string(f);
    }

    // If the next character is a newline, we increment the current line and reset
    // the column number.
    if (c == '\n') {
        f->line++;
        f->column = 1;
    
    // If the character is anything except the end of the file, increment the 
    // column tracker.
    } else if (c != EOF) {
        f->column++;
    }
    return c;
}

// Public function to read a character.
int readc() {
    for (;;) {
        // Get the next character.
        int c = get();

        if (c == EOF) {
            // If this is the last/only file, just return EOF.
            if (vec_len(files) == 1)
                return c;
            // Close the file while popping the current file off the vector stack.
            // Then, we continue reading at the start of the next file.
            close_file(vec_pop(files));
            continue;
        }
        
        // We need to merge lines that are terminated with \ into one long line,
        // as per the C specification. Basically, we look at the character after
        // the \. If it's a newline, then we skip the \ and the newline.
        // Otherwise, we put the character back onto the stream and return the \.
        if (c != '\\')
            return c;
        int c2 = get();
        if (c2 == '\n')
            continue;
        unreadc(c2);

        return c;
    }
}

// unreadc() puts a character back onto the stream.
void unreadc(int c) {
    if (c == EOF)
        return;
    
    // Get the last file.
    File *f = vec_tail(files);

    // Assert that the buffer hasn't somehow got a length longer than the amount
    // of memory that was allocated for it.
    assert(f->buflen < sizeof(f->buf) / sizeof(f->buf[0]));
    
    // Put the character into the buffer.
    f->buf[f->buflen++] = c;

    // If the character is a newline, reset the column and decrement the 
    // current line. Otherwise, decrement the column.
    if (c == '\n') {
        f->column = 1;
        f->line--;
    } else {
        f->column--;
    }
}

// Get the current file.
File *current_file() {
    return vec_tail(files);
}

// Push another file onto the files vector.
void stream_push(File *f) {
    vec_push(files, f);
}

// Get the number of files.
int stream_depth() {
    return vec_len(files);
}

// Print the current filename, line number, and column.
char *input_position() {
    if (vec_len(files) == 0)
        return "(unknown)";
    File *f = vec_tail(files);
    return format("%s:%d:%d", f->name, f->line, f->column);
}

// Stash the current files and push a file onto the vector. 
void stream_stash(File *f) {
    vec_push(stashed, files);
    files = make_vector1(f);
}

// Unstash the stashed files.
void stream_unstash() {
    files = vec_pop(stashed);
}
