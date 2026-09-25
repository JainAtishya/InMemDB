#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "persistence.h"

#define AOF_FILE "data.aof"

int persistence_append_set(
    const char *key,
    const char *value
)
{
    return persistence_append_set_expiry(
        key,
        value,
        0
    );
}

int persistence_append_set_expiry(
    const char *key,
    const char *value,
    time_t expires_at
)
{
    FILE *file =
        fopen(AOF_FILE, "a");

    if (file == NULL)
    {
        return 0;
    }

    size_t key_length =
        strlen(key);

    size_t value_length =
        strlen(value);

    int result;

    if (expires_at == 0)
    {
        result = fprintf(
            file,
            "SET %zu:%s%zu:%s\n",
            key_length,
            key,
            value_length,
            value
        );
    }
    else
    {
        result = fprintf(
            file,
            "SET %zu:%s%zu:%s EXAT %lld\n",
            key_length,
            key,
            value_length,
            value,
            (long long)expires_at
        );
    }

    if (result < 0)
    {
        fclose(file);
        return 0;
    }

    if (fclose(file) != 0)
    {
        return 0;
    }

    return 1;
}

int persistence_append_del(
    const char *key
)
{
    FILE *file =
        fopen(AOF_FILE, "a");

    if (file == NULL)
    {
        return 0;
    }

    size_t key_length =
        strlen(key);

    if (
        fprintf(
            file,
            "DEL %zu:%s\n",
            key_length,
            key
        ) < 0
    )
    {
        fclose(file);
        return 0;
    }

    if (fclose(file) != 0)
    {
        return 0;
    }

    return 1;
}

int persistence_load(
    HashTable *table
)
{
    FILE *file =
        fopen(AOF_FILE, "r");

    /*
     * No AOF yet.
     */
    if (file == NULL)
    {
        return 1;
    }

    char command[4];

    while (
        fscanf(
            file,
            "%3s",
            command
        ) == 1
    )
    {
        if (strcmp(command, "SET") == 0)
        {
            size_t key_length;
            size_t value_length;

            /*
             * Read key length.
             */
            if (
                fscanf(
                    file,
                    "%zu",
                    &key_length
                ) != 1
            )
            {
                fclose(file);
                return 0;
            }

            if (fgetc(file) != ':')
            {
                fclose(file);
                return 0;
            }

            char *key =
                malloc(key_length + 1);

            if (key == NULL)
            {
                fclose(file);
                return 0;
            }

            if (
                fread(
                    key,
                    1,
                    key_length,
                    file
                ) != key_length
            )
            {
                free(key);
                fclose(file);
                return 0;
            }

            key[key_length] = '\0';

            /*
             * Read value length.
             */
            if (
                fscanf(
                    file,
                    "%zu",
                    &value_length
                ) != 1
            )
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

            char *value =
                malloc(value_length + 1);

            if (value == NULL)
            {
                free(key);
                fclose(file);
                return 0;
            }

            if (
                fread(
                    value,
                    1,
                    value_length,
                    file
                ) != value_length
            )
            {
                free(key);
                free(value);
                fclose(file);
                return 0;
            }

            value[value_length] = '\0';

            /*
             * Read optional expiration information.
             *
             * Old format:
             *
             * SET key value\n
             *
             * New format:
             *
             * SET key value EXAT timestamp\n
             */
            int next_char = fgetc(file);

            time_t expires_at = 0;

            if (next_char == '\n')
            {
                /*
                 * Old SET without TTL.
                 */
                expires_at = 0;
            }
            else if (next_char == ' ')
            {
                char expiry_command[5];

                long long expiry_timestamp;

                if (
                    fscanf(
                        file,
                        "%4s %lld",
                        expiry_command,
                        &expiry_timestamp
                    ) != 2
                )
                {
                    free(key);
                    free(value);
                    fclose(file);
                    return 0;
                }

                if (
                    strcmp(
                        expiry_command,
                        "EXAT"
                    ) != 0
                )
                {
                    free(key);
                    free(value);
                    fclose(file);
                    return 0;
                }

                if (fgetc(file) != '\n')
                {
                    free(key);
                    free(value);
                    fclose(file);
                    return 0;
                }

                expires_at =
                    (time_t)expiry_timestamp;
            }
            else
            {
                free(key);
                free(value);
                fclose(file);
                return 0;
            }

            /*
             * If the key has already expired while
             * the server was offline, don't restore it.
             */
            if (
                expires_at != 0 &&
                time(NULL) >= expires_at
            )
            {
                free(key);
                free(value);
                continue;
            }

            if (
                !hash_table_set_with_expiry(
                    table,
                    key,
                    value,
                    expires_at
                )
            )
            {
                free(key);
                free(value);
                fclose(file);
                return 0;
            }

            free(key);
            free(value);
        }

        else if (strcmp(command, "DEL") == 0)
        {
            size_t key_length;

            if (
                fscanf(
                    file,
                    "%zu",
                    &key_length
                ) != 1
            )
            {
                fclose(file);
                return 0;
            }

            if (fgetc(file) != ':')
            {
                fclose(file);
                return 0;
            }

            char *key =
                malloc(key_length + 1);

            if (key == NULL)
            {
                fclose(file);
                return 0;
            }

            if (
                fread(
                    key,
                    1,
                    key_length,
                    file
                ) != key_length
            )
            {
                free(key);
                fclose(file);
                return 0;
            }

            key[key_length] = '\0';

            hash_table_delete(
                table,
                key
            );

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

int persistence_rewrite(
    HashTable *table
)
{
    FILE *file =
        fopen(
            "data.aof.tmp",
            "w"
        );

    if (file == NULL)
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
            /*
             * Don't rewrite already expired keys.
             */
            if (!entry_is_expired(entry))
            {
                size_t key_length =
                    strlen(entry->key);

                size_t value_length =
                    strlen(entry->value);

                int result;

                if (entry->expires_at == 0)
                {
                    result = fprintf(
                        file,
                        "SET %zu:%s%zu:%s\n",
                        key_length,
                        entry->key,
                        value_length,
                        entry->value
                    );
                }
                else
                {
                    result = fprintf(
                        file,
                        "SET %zu:%s%zu:%s EXAT %lld\n",
                        key_length,
                        entry->key,
                        value_length,
                        entry->value,
                        (long long)entry->expires_at
                    );
                }

                if (result < 0)
                {
                    fclose(file);
                    return 0;
                }
            }

            entry = entry->next;
        }
    }

    if (fclose(file) != 0)
    {
        return 0;
    }

    if (
        rename(
            "data.aof.tmp",
            AOF_FILE
        ) != 0
    )
    {
        return 0;
    }

    return 1;
}