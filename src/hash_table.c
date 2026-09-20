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

const char *hash_table_get(HashTable *table, const char *key)
{
    size_t index = hash_key(key) % table->size;

    Entry *entry = table->buckets[index];

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            return entry->value;
        }

        entry = entry->next;
    }

    return NULL;
}

int hash_table_delete(HashTable *table, const char *key)
{
    if (table == NULL || key == NULL)
    {
        return 0;
    }

    size_t index = hash_key(key) % table->size;

    Entry *current = table->buckets[index];
    Entry *previous = NULL;

    while (current != NULL)
    {
        if (strcmp(current->key, key) == 0)
        {
            // Case 1: deleting the first node
            if (previous == NULL)
            {
                table->buckets[index] = current->next;
            }
            // Case 2: deleting a node in the middle/end
            else
            {
                previous->next = current->next;
            }

            entry_destroy(current);
            table->count--;

            return 1;
        }

        previous = current;
        current = current->next;
    }

    return 0;
}