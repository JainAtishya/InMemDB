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

    hash_table_set(table, "name", "Atishya");
    hash_table_set(table, "age", "21");
    hash_table_set(table, "city", "Delhi");

    printf("Before delete:\n");

    printf("name = %s\n", hash_table_get(table, "name"));
    printf("age = %s\n", hash_table_get(table, "age"));
    printf("city = %s\n", hash_table_get(table, "city"));

    printf("\nDeleting age...\n");

    if (hash_table_delete(table, "age"))
    {
        printf("age deleted successfully\n");
    }
    else
    {
        printf("age not found\n");
    }

    printf("\nAfter delete:\n");

    printf("name = %s\n", hash_table_get(table, "name"));
    printf("age = %s\n", hash_table_get(table, "age"));
    printf("city = %s\n", hash_table_get(table, "city"));

    printf("\nDeleting unknown key...\n");

    if (hash_table_delete(table, "unknown"))
    {
        printf("deleted\n");
    }
    else
    {
        printf("Key not found\n");
    }

    hash_table_destroy(table);

    return 0;
}