#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "persistence.h"

#define AOF_FILE "data.aof"


int persistence_append_set(const char *key, const char *value)
{
    FILE *file = fopen(AOF_FILE, "a");

    if (file == NULL)
    {
        return 0;
    }

    size_t key_length = strlen(key);
    size_t value_length = strlen(value);

    fprintf(file, "SET %zu:%s%zu:%s\n",
            key_length,
            key,
            value_length,
            value);

    fclose(file);

    return 1;
}


int persistence_append_del(const char *key)
{
    FILE *file = fopen(AOF_FILE, "a");

    if (file == NULL)
    {
        return 0;
    }

    size_t key_length = strlen(key);

    fprintf(file, "DEL %zu:%s\n",
            key_length,
            key);

    fclose(file);

    return 1;
}


int persistence_load(HashTable *table)
{
    FILE *file = fopen(AOF_FILE, "r");

    if (file == NULL)
    {
        return 1;
    }

    char command[4];

    while (fscanf(file, "%3s", command) == 1)
    {
        if (strcmp(command, "SET") == 0)
        {
            size_t key_length;
            size_t value_length;

            if (fscanf(file, "%zu", &key_length) != 1)
            {
                fclose(file);
                return 0;
            }

            if (fgetc(file) != ':')
            {
                fclose(file);
                return 0;
            }

            char *key = malloc(key_length + 1);

            if (key == NULL)
            {
                fclose(file);
                return 0;
            }

            if (fread(key, 1, key_length, file) != key_length)
            {
                free(key);
                fclose(file);
                return 0;
            }

            key[key_length] = '\0';

            if (fscanf(file, "%zu", &value_length) != 1)
            {
                free(key);
                fclose(file);
                return 0;
            }

            if (fgetc(file) != ':')
            {
                free(key);
                fclose(file);
                return 0;
            }

            char *value = malloc(value_length + 1);

            if (value == NULL)
            {
                free(key);
                fclose(file);
                return 0;
            }

            if (fread(value, 1, value_length, file) != value_length)
            {
                free(key);
                free(value);
                fclose(file);
                return 0;
            }

            value[value_length] = '\0';

            if (!hash_table_set(table, key, value))
            {
                free(key);
                free(value);
                fclose(file);
                return 0;
            }

            free(key);
            free(value);

            if (fgetc(file) != '\n')
            {
                fclose(file);
                return 0;
            }
        }
        else if (strcmp(command, "DEL") == 0)
        {
            size_t key_length;

            if (fscanf(file, "%zu", &key_length) != 1)
            {
                fclose(file);
                return 0;
            }

            if (fgetc(file) != ':')
            {
                fclose(file);
                return 0;
            }

            char *key = malloc(key_length + 1);

            if (key == NULL)
            {
                fclose(file);
                return 0;
            }

            if (fread(key, 1, key_length, file) != key_length)
            {
                free(key);
                fclose(file);
                return 0;
            }

            key[key_length] = '\0';

            hash_table_delete(table, key);

            free(key);

            if (fgetc(file) != '\n')
            {
                fclose(file);
                return 0;
            }
        }
        else
        {
            fclose(file);
            return 0;
        }
    }

    fclose(file);

    return 1;
}

int persistence_rewrite(HashTable *table)
{
    FILE *file = fopen("data.aof.tmp", "w");

    if (file == NULL)
    {
        return 0;
    }

    for (size_t i = 0; i < table->size; i++)
    {
        Entry *entry = table->buckets[i];

        while (entry != NULL)
        {
            size_t key_length = strlen(entry->key);
            size_t value_length = strlen(entry->value);

            fprintf(file, "SET %zu:%s%zu:%s\n",
                    key_length,
                    entry->key,
                    value_length,
                    entry->value);

            entry = entry->next;
        }
    }

    if (fclose(file) != 0)
    {
        return 0;
    }

    if (rename("data.aof.tmp", AOF_FILE) != 0)
    {
        return 0;
    }

    return 1;
}