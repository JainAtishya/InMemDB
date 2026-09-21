#include <stdio.h>

#include "hash_table.h"
#include "command.h"

int main(void)
{
    HashTable *table = hash_table_create(8);

    if (table == NULL)
    {
        printf("Failed to create database\n");
        return 1;
    }

    printf("InMemDB started.\n");
    printf("Type EXIT to quit.\n\n");

    command_loop(table);

    hash_table_destroy(table);

    printf("InMemDB stopped.\n");

    return 0;
}