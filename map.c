// Copyright 2014 Rui Ueyama. Released under the MIT license.

// This is an implementation of hash table.
// Specifically, this implementation is of an open-addressing hash table with
// linear probing.

#include <stdlib.h>
#include <string.h>
#include "8cc.h"

#define INIT_SIZE 16

// This is a macro that defines a pointer value that is greater than any other
// address on the system, and (presumably) an address that we can't write to.
// It utilizes undefined C behaviour: it should underflow (void *) to be the 
// maximum value.   
#define TOMBSTONE ((void *)-1)

// Hash function. Specifically, this function implements the FNV hash, a fast
// non-cryptographic hash function.
static uint32_t hash(char *p) {
    // FNV hash
    uint32_t r = 2166136261;
    for (; *p; p++) {
        r ^= *p;
        r *= 16777619;
    }
    return r;
}

// Create a map of a certain size.
static Map *do_make_map(Map *parent, int size) {
    // Allocate memory for the map struct.
    Map *r = malloc(sizeof(Map));

    // Set the parent of the new map to the provided map.
    r->parent = parent;

    // calloc() allocates memory for a number of items and zeroes it.
    // Note that the keys are a pointer to char, while the values can be
    // anything (pointer to void).
    r->key = calloc(size, sizeof(char *));
    r->val = calloc(size, sizeof(void *));

    // Set the size, number of elements, number of used and return.
    r->size = size;
    r->nelem = 0;
    r->nused = 0;
    return r;
}

static void maybe_rehash(Map *m) {
    // The base case: if there's no key, we allocate the minimum amount of 
    // memory for each of the properties.
    if (!m->key) {
        m->key = calloc(INIT_SIZE, sizeof(char *));
        m->val = calloc(INIT_SIZE, sizeof(void *));
        m->size = INIT_SIZE;
        return;
    }

    // If the amount of used memory is less than 70% of the total amount of memory,
    // then there's no need to reallocate. This variable is called the 'load 
    // factor', and it's very important for the performance of the hash map.
    // Adjusting this factor changes how frequently we need to rehash and how 
    // efficient the lookups we do are.  
    if (m->nused < m->size * 0.7)
        return;
    
    // The new size of the hashmap is 
    int newsize = (m->nelem < m->size * 0.35) ? m->size : m->size * 2;

    // Allocate memory for new arrays of char* and void* for storing keys and
    // values.
    char **k = calloc(newsize, sizeof(char *));
    void **v = calloc(newsize, sizeof(void *));

    // Since the size of the map is a power of two, the value one less than the
    // size can act as an effective mask. Consider that the size of the map is
    // 0b001000000. The value of size - 1 would be 0b000111111. 
    int mask = newsize - 1;

    // Iterate through all of the keys and the values.
    for (int i = 0; i < m->size; i++) {
        // If a key is null, we don't include it in the reallocation.
        if (m->key[i] == NULL || m->key[i] == TOMBSTONE)
            continue;

        // We can then use the mask we created earlier to restrict the range of
        // the output of the hash function to only the values in the  
        int j = hash(m->key[i]) & mask;

        // We also need to reallocate any of the data that is next to a given 
        // key. We increase the current hash value, taking care to restrict it
        // to the correct range (to avoid accessing null memory).
        for (;; j = (j + 1) & mask) {
            if (k[j] != NULL)
                continue;
            k[j] = m->key[i];
            v[j] = m->val[i];
            break;
        }
    }
    // Set up the new properties of the map.
    m->key = k;
    m->val = v;
    m->size = newsize;
    m->nused = m->nelem;
}

// Create a new map.
Map *make_map() {
    return do_make_map(NULL, INIT_SIZE);
}

// Create a new map with the supplied parent.
Map *make_map_parent(Map *parent) {
    return do_make_map(parent, INIT_SIZE);
}

// Internal function to get a value from this particular map (ignore parents).
static void *map_get_nostack(Map *m, char *key) {
    // If there aren't any keys, return null.
    if (!m->key)
        return NULL;

    // The mask is one less than the size (see comment above).
    int mask = m->size - 1;

    // Calculate the hash of the key, taking into account the mask.
    int i = hash(key) & mask;

    // Look at all adjacent values and check if they contain the key we're 
    // looking for. Note that we only terminate the search when we encounter
    // a NULL key, but not a key that has a TOMBSTONE key. This is because we
    // need to be able to know when to stop looking for an element. The TOMBSTONE
    // key represents an item that has been deleted; there might be items beyond
    // this item that are still valid, so we can't stop searching once we reach
    // a TOMBSTONE key.
    for (; m->key[i] != NULL; i = (i + 1) & mask)
        if (m->key[i] != TOMBSTONE && !strcmp(m->key[i], key))
            return m->val[i];
    
    // We couldn't find the given key, so we return null.
    return NULL;
}

// Get a value from the map.
void *map_get(Map *m, char *key) {
    // Check this map first.
    void *r = map_get_nostack(m, key);
    if (r)
        return r;
    // Map is stackable. If no value is found,
    // continue searching from the parent.
    if (m->parent)
        return map_get(m->parent, key);
    return NULL;
}

// Insert a value into the map.
void map_put(Map *m, char *key, void *val) {
    // Check if we need to rehash and expand the map first. We don't want 
    // too many collisions!
    maybe_rehash(m);

    // Create the mask.
    int mask = m->size - 1;

    // Calculate the hash of the key, taking into account the mask.
    int i = hash(key) & mask;

    // Look at all of the adjacent values until we find one that we can
    // insert into. 
    for (;; i = (i + 1) & mask) {
        // Create a temp variable for the current key.
        char *k = m->key[i];
        
        // If the current 'slot' isn't filled, we fill it.
        if (k == NULL || k == TOMBSTONE) {
            // Set the key and the value.
            m->key[i] = key;
            m->val[i] = val;
            m->nelem++;

            // Keys that are removed are set to the TOMBSTONE instead of NULL,
            // because 'nused' is a measure of how many slots in the hashmap have
            // been used at any point, rather than merely at the current time.
            if (k == NULL)
                m->nused++;
            return;
        }

        // Otherwise, we check to see if the key already exists and update it
        // if it does.
        if (!strcmp(k, key)) {
            m->val[i] = val;
            return;
        }
    }
}

// Remove a key from the map.
void map_remove(Map *m, char *key) {
    if (!m->key)
        return;
    
    // Calculate the mask.
    int mask = m->size - 1;

    // Calculate the hash.
    int i = hash(key) & mask;

    // Look at the adjacent keys
    for (; m->key[i] != NULL; i = (i + 1) & mask) {
        // If this is the wrong key, we continue.
        if (m->key[i] == TOMBSTONE || strcmp(m->key[i], key))
            continue;

        // Key that is removed is set to the TOMBSTONE instead of NULL.
        // This means that they are still included in the measure of how many
        // of the elements are 'used', and also it means that we can still search
        // through adjacent items to find the right item (because there won't be
        // any gaps).
        m->key[i] = TOMBSTONE;
        m->val[i] = NULL;
        m->nelem--;
        return;
    }
}

// Get the number of elements in the map.
size_t map_len(Map *m) {
    return m->nelem;
}
