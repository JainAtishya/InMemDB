#include <stdio.h>

#include "hash_table.h"
#include "server.h"
#include "persistence.h"

int main(void)
{
    HashTable *table =
        hash_table_create(8);

    if (table == NULL)
    {
        printf("Failed to create database\n");

        return 1;
    }


    /* Load persisted data */

    if (!persistence_load(table))
    {
        printf("Failed to load AOF\n");

        hash_table_destroy(table);

        return 1;
    }


    printf("InMemDB started.\n");
    printf("Server starting on 127.0.0.1:6379\n");


    if (!server_start(6379, table))
    {
        printf("Server failed to start.\n");

        hash_table_destroy(table);

        return 1;
    }


    hash_table_destroy(table);

    printf("InMemDB stopped.\n");

    return 0;
}