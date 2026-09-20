#ifndef HASH_TABLE_H
#define HASH_TABLE_H
#define LOAD_FACTOR 0.75
#define RESIZE_FACTOR 2

#include <stddef.h>
#include "entry.h"

typedef struct HashTable {
    Entry **buckets;
    size_t size;
    size_t count;
} HashTable;

HashTable *hash_table_create(size_t size);
void hash_table_destroy(HashTable *table);
size_t hash_key(const char *key);
int hash_table_set(HashTable *table, const char *key, const char *value);
const char *hash_table_get(HashTable *table, const char *key);
int hash_table_delete(HashTable *table, const char *key);
int hash_table_resize(HashTable *table, size_t new_size);

#endif