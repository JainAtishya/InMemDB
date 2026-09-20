#include <stdio.h>
#include "hash_table.h"

int main(void)
{
    HashTable *table = hash_table_create(10);

    if (table == NULL)
    {
        printf("Failed to create hash table\n");
        return 1;
    }

    printf("Hash table created\n");
    printf("Size: %zu\n", table->size);
    printf("Count: %zu\n\n", table->count);

    /* Insert */
    printf("Setting name = Atishya\n");
    hash_table_set(table, "name", "Atishya");

    printf("Count: %zu\n", table->count);

    /* Update */
    printf("Updating name = Rahul\n");
    hash_table_set(table, "name", "Rahul");

    printf("Count: %zu\n", table->count);

    /* Find the bucket directly for testing */
    size_t index = hash_key("name") % table->size;

    Entry *entry = table->buckets[index];

    printf("\nBucket index for 'name': %zu\n", index);
    printf("Key: %s\n", entry->key);
    printf("Value: %s\n", entry->value);

    hash_table_destroy(table);

    return 0;
}