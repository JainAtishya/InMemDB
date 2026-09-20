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

    printf("SET name = Atishya\n");
    hash_table_set(table, "name", "Atishya");

    const char *value = hash_table_get(table, "name");

    if (value != NULL)
    {
        printf("GET name = %s\n", value);
    }
    else
    {
        printf("Key not found\n");
    }

    printf("\nUpdating name = Rahul\n");
    hash_table_set(table, "name", "Rahul");

    value = hash_table_get(table, "name");

    if (value != NULL)
    {
        printf("GET name = %s\n", value);
    }
    else
    {
        printf("Key not found\n");
    }

    printf("\nGET unknown:\n");

    value = hash_table_get(table, "unknown");

    if (value != NULL)
    {
        printf("Value = %s\n", value);
    }
    else
    {
        printf("Key not found\n");
    }

    hash_table_destroy(table);

    return 0;
}