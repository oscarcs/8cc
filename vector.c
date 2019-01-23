// Copyright 2012 Rui Ueyama. Released under the MIT license.

/*
 * Vectors are containers of void pointers that can change in size.
 */

#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define MIN_SIZE 8

// Returns the larger of two integers. Obviously.
// Something you might not know: by marking this function as static, we make it 
// accessible only within the current 'compilation unit'. A compilation unit is
// what we're left with after the C preprocessor has its way with our source 
// files, i.e. after all the #includes have been resolved.
static int max(int a, int b) {
    return a > b ? a : b;
}

// Rounds to the nearest number that is a power of 2.
// You could implement this more efficiently using some bit-twiddling, but the
// purpose of the 8cc compiler is to be highly readable.
static int roundup(int n) {
    if (n == 0)
        return 0;

    // Loop to find the first r larger than n.
    int r = 1;
    while (n > r)
        r *= 2;
    return r;
}

// Creates a vector of a certain size; only to be used internally.
static Vector *do_make_vector(int size) {
    // Allocate memory for the Vector struct.
    Vector *r = malloc(sizeof(Vector));

    // Round the size up to a neat power of 2.
    size = roundup(size);

    // If the size is nonzero, we preallocate memory for the body of the vector.
    // We allocate based on the size of "void *" - a 'void pointer'. This is a
    // pointer that can point to memory containing any type.
    if (size > 0)
        r->body = malloc(sizeof(void *) * size);

    // Set the length of the vector and the number of allocated bytes.
    r->len = 0;
    r->nalloc = size;
    return r;
}

// Create an empty vector.
Vector *make_vector() {
    return do_make_vector(0);
}

// Extend the size of the vector.
static void extend(Vector *vec, int delta) {
    // If the new length of the vector is less than the number of bytes already
    // allocated, then we're done.
    if (vec->len + delta <= vec->nalloc)
        return;

    // Otherwise we round up the new size to the nearest power of 2 that's at 
    // least as large at the minimum.
    int nelem = max(roundup(vec->len + delta), MIN_SIZE);
    
    // We allocate the new block of memory...
    void *newbody = malloc(sizeof(void *) * nelem);

    // ...and copy the existing memory into the new memory.
    memcpy(newbody, vec->body, sizeof(void *) * vec->len);

    // Then we update the body and set the new length.
    vec->body = newbody;
    vec->nalloc = nelem;
}

// Helper function to create a vector and immediately store something into it.
Vector *make_vector1(void *e) {
    Vector *r = do_make_vector(0);
    vec_push(r, e);
    return r;
}

// Make a copy of a vector.
Vector *vec_copy(Vector *src) {
    // Create a vector of the same size.
    Vector *r = do_make_vector(src->len);

    // Copy the values of the vector's fields.
    memcpy(r->body, src->body, sizeof(void *) * src->len);
    r->len = src->len;
    return r;
}

// Push an element onto the vector.
void vec_push(Vector *vec, void *elem) {
    // Extend the size of the vector.
    extend(vec, 1);

    // Copy the data onto the vector.
    vec->body[vec->len++] = elem;
}

// Append one vector onto another vector.
void vec_append(Vector *a, Vector *b) {
    // Extend the vector by the length of the first vector.
    extend(a, b->len);

    // Copy the contents of the second vector onto the first vector.
    memcpy(a->body + a->len, b->body, sizeof(void *) * b->len);

    // Increase the length of a by b.
    a->len += b->len;
}

// Pop an element off the vector.
void *vec_pop(Vector *vec) {
    // Check that the length of the vector is greater than 0.
    // If it isn't (i.e. the vector is empty), print an error message and exit
    // the program.
    assert(vec->len > 0);

    // Return the last item and decrement the length of the vector.
    return vec->body[--vec->len];
}

// Get the item at a particular index.
void *vec_get(Vector *vec, int index) {
    // Assert that the index is between 0 and the length of the vector.
    assert(0 <= index && index < vec->len);

    // Return the item.
    return vec->body[index];
}

// Set the contents of a particular item at a given index.
void vec_set(Vector *vec, int index, void *val) {
    // Assert that the index is between 0 and the length of the vector,
    assert(0 <= index && index < vec->len);

    // Replace the item in question.
    vec->body[index] = val;
}

// Get the first item.
void *vec_head(Vector *vec) {
    assert(vec->len > 0);
    return vec->body[0];
}

// Get the last item.
void *vec_tail(Vector *vec) {
    assert(vec->len > 0);
    return vec->body[vec->len - 1];
}

// Return a new vector with the order of the elements reversed.
Vector *vec_reverse(Vector *vec) {
    // Make a new vector of the same length.
    Vector *r = do_make_vector(vec->len);

    // Loop through the elements in the vector...
    for (int i = 0; i < vec->len; i++)
        // ...swapping each element.
        r->body[i] = vec->body[vec->len - i - 1];
    
    // Set the length of the new vector and return.
    r->len = vec->len;
    return r;
}

// Return the body of the vector.
void *vec_body(Vector *vec) {
    return vec->body;
}

// Return the length of the vector.
int vec_len(Vector *vec) {
    return vec->len;
}
