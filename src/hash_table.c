#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

HashTable *hash_table_create(size_t size)
{
    HashTable *table = malloc(sizeof(HashTable));

    if (table == NULL) {
        return NULL;
    }

    table->buckets = calloc(size, sizeof(Entry *));

    if (table->buckets == NULL) {
        free(table);
        return NULL;
    }

    table->size = size;
    table->count = 0;

    return table;
}

void hash_table_destroy(HashTable *table)
{
    if (table == NULL) {
        return;
    }

    free(table->buckets);
    free(table);
}

size_t hash_key(const char *key)
{
    size_t hash = 0;

    while (*key != '\0')
    {
        hash = hash * 31 + (unsigned char)*key;
        key++;
    }

    return hash;
}

int hash_table_set(HashTable *table, const char *key, const char *value)
{
    size_t index = hash_key(key) % table->size;

    Entry *entry = table->buckets[index];

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            char *new_value = malloc(strlen(value) + 1);

            if (new_value == NULL)
            {
                return 0;
            }

            strcpy(new_value, value);

            free(entry->value);

            entry->value = new_value;

            return 1;
        }

        entry = entry->next;
    }

    entry = malloc(sizeof(Entry));

    if (entry == NULL)
    {
        return 0;
    }

    entry->key = malloc(strlen(key) + 1);

    if (entry->key == NULL)
    {
        free(entry);
        return 0;
    }

    strcpy(entry->key, key);

    entry->value = malloc(strlen(value) + 1);

    if (entry->value == NULL)
    {
        free(entry->key);
        free(entry);
        return 0;
    }

    strcpy(entry->value, value);

    entry->next = table->buckets[index];

    table->buckets[index] = entry;

    table->count++;

    return 1;
}