#include <stdio.h>
#include "hash_table.h"

int main(void)
{
    HashTable *table = hash_table_create(4);

    if (table == NULL)
    {
        printf("Failed to create hash table\n");
        return 1;
    }

    printf("=== INSERT ===\n");

    hash_table_set(table, "name", "Atishya");
    hash_table_set(table, "age", "21");
    hash_table_set(table, "city", "Delhi");
    hash_table_set(table, "college", "Chitkara");

    printf("name = %s\n", hash_table_get(table, "name"));
    printf("age = %s\n", hash_table_get(table, "age"));
    printf("city = %s\n", hash_table_get(table, "city"));
    printf("college = %s\n", hash_table_get(table, "college"));

    printf("\n=== UPDATE ===\n");

    hash_table_set(table, "age", "22");

    printf("age = %s\n", hash_table_get(table, "age"));

    printf("\n=== DELETE ===\n");

    hash_table_delete(table, "city");

    printf("city = %s\n", hash_table_get(table, "city"));

    if (hash_table_get(table, "city") == NULL)
    {
        printf("city successfully deleted\n");
    }

    printf("\n=== RESIZE ===\n");

    printf("Before resize:\n");
    printf("size = %zu\n", table->size);
    printf("count = %zu\n", table->count);

    hash_table_resize(table, 8);

    printf("After resize:\n");
    printf("size = %zu\n", table->size);
    printf("count = %zu\n", table->count);

    printf("\nChecking data after rehash:\n");

    printf("name = %s\n", hash_table_get(table, "name"));
    printf("age = %s\n", hash_table_get(table, "age"));
    printf("college = %s\n", hash_table_get(table, "college"));

    printf("\n=== DESTROY ===\n");

    hash_table_destroy(table);

    printf("Hash table destroyed successfully\n");

    return 0;
}