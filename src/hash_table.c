#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hash_table.h"

HashTable *hash_table_create(
    size_t size
)
{
    HashTable *table = malloc(sizeof(HashTable));

    if (table == NULL)
    {
        return NULL;
    }

    table->buckets =
        calloc(size, sizeof(Entry *));

    if (table->buckets == NULL)
    {
        free(table);
        return NULL;
    }

    table->size = size;
    table->count = 0;

    return table;
}

void hash_table_destroy(
    HashTable *table
)
{
    if (table == NULL)
    {
        return;
    }

    for (size_t i = 0; i < table->size; i++)
    {
        Entry *current = table->buckets[i];

        while (current != NULL)
        {
            Entry *next = current->next;

            entry_destroy(current);

            current = next;
        }
    }

    free(table->buckets);
    free(table);
}

size_t hash_key(
    const char *key
)
{
    size_t hash = 0;

    while (*key != '\0')
    {
        hash =
            hash * 31 +
            (unsigned char)*key;

        key++;
    }

    return hash;
}

int hash_table_set(
    HashTable *table,
    const char *key,
    const char *value
)
{
    /*
     * Existing SET behavior remains unchanged:
     * no expiration.
     */
    return hash_table_set_with_expiry(
        table,
        key,
        value,
        0
    );
}

int hash_table_set_with_expiry(
    HashTable *table,
    const char *key,
    const char *value,
    time_t expires_at
)
{
    if (
        table == NULL ||
        key == NULL ||
        value == NULL
    )
    {
        return 0;
    }

    size_t index =
        hash_key(key) % table->size;

    Entry *entry =
        table->buckets[index];

    /*
     * Key already exists.
     */
    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            char *new_value =
                malloc(strlen(value) + 1);

            if (new_value == NULL)
            {
                return 0;
            }

            strcpy(new_value, value);

            free(entry->value);

            entry->value = new_value;

            /*
             * Update expiration as well.
             *
             * This is important because:
             *
             * SET key newvalue
             *
             * must remove any previous TTL.
             */
            entry_set_expiry_at(
                entry,
                expires_at
            );

            return 1;
        }

        entry = entry->next;
    }

    /*
     * Key does not exist.
     */
    entry = malloc(sizeof(Entry));

    if (entry == NULL)
    {
        return 0;
    }

    entry->key =
        malloc(strlen(key) + 1);

    if (entry->key == NULL)
    {
        free(entry);
        return 0;
    }

    strcpy(entry->key, key);

    entry->value =
        malloc(strlen(value) + 1);

    if (entry->value == NULL)
    {
        free(entry->key);
        free(entry);
        return 0;
    }

    strcpy(entry->value, value);

    entry->expires_at = expires_at;

    entry->next =
        table->buckets[index];

    table->buckets[index] = entry;

    table->count++;

    double load_factor =
        (double)table->count /
        table->size;

    if (load_factor > LOAD_FACTOR)
    {
        hash_table_resize(
            table,
            table->size * RESIZE_FACTOR
        );
    }

    return 1;
}

const char *hash_table_get(
    HashTable *table,
    const char *key
)
{
    if (
        table == NULL ||
        key == NULL
    )
    {
        return NULL;
    }

    size_t index =
        hash_key(key) % table->size;

    Entry *entry =
        table->buckets[index];

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            /*
             * Lazy expiration.
             *
             * We do NOT delete the entry here because
             * GET can execute under the read lock.
             */
            if (entry_is_expired(entry))
            {
                return NULL;
            }

            return entry->value;
        }

        entry = entry->next;
    }

    return NULL;
}

int hash_table_delete(
    HashTable *table,
    const char *key
)
{
    if (
        table == NULL ||
        key == NULL
    )
    {
        return 0;
    }

    size_t index =
        hash_key(key) % table->size;

    Entry *current =
        table->buckets[index];

    Entry *previous = NULL;

    while (current != NULL)
    {
        if (strcmp(current->key, key) == 0)
        {
            /*
             * Case 1:
             * deleting first node.
             */
            if (previous == NULL)
            {
                table->buckets[index] =
                    current->next;
            }

            /*
             * Case 2:
             * deleting middle/end node.
             */
            else
            {
                previous->next =
                    current->next;
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

int hash_table_resize(
    HashTable *table,
    size_t new_size
)
{
    if (
        table == NULL ||
        new_size == 0
    )
    {
        return 0;
    }

    Entry **new_buckets =
        calloc(
            new_size,
            sizeof(Entry *)
        );

    if (new_buckets == NULL)
    {
        return 0;
    }

    for (
        size_t i = 0;
        i < table->size;
        i++
    )
    {
        Entry *entry =
            table->buckets[i];

        while (entry != NULL)
        {
            Entry *next =
                entry->next;

            size_t new_index =
                hash_key(entry->key) %
                new_size;

            entry->next =
                new_buckets[new_index];

            new_buckets[new_index] =
                entry;

            entry = next;
        }
    }

    free(table->buckets);

    table->buckets = new_buckets;
    table->size = new_size;

    return 1;
}

long long hash_table_ttl(
    HashTable *table,
    const char *key
)
{
    if (table == NULL || key == NULL)
    {
        return -2;
    }

    size_t index = hash_key(key) % table->size;

    Entry *entry = table->buckets[index];

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            /*
             * Key exists but has no expiration.
             */
            if (entry->expires_at == 0)
            {
                return -1;
            }

            time_t now = time(NULL);

            /*
             * Key has expired.
             */
            if (now >= entry->expires_at)
            {
                return -2;
            }

            return (long long)(entry->expires_at - now);
        }

        entry = entry->next;
    }

    /*
     * Key does not exist.
     */
    return -2;
}