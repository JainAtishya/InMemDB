#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "command.h"
#include "persistence.h"
#include "entry.h"

#define INPUT_SIZE 256
#define MAX_ARGS 5


/**
 * Parse a command string into argv-style arguments.
 *
 * Examples:
 *
 * SET name Atishya
 *
 * argv[0] = "SET"
 * argv[1] = "name"
 * argv[2] = "Atishya"
 *
 * SET message "Hello World"
 *
 * argv[0] = "SET"
 * argv[1] = "message"
 * argv[2] = "Hello World"
 *
 * SET name Atishya EX 10
 *
 * argv[0] = "SET"
 * argv[1] = "name"
 * argv[2] = "Atishya"
 * argv[3] = "EX"
 * argv[4] = "10"
 */
int parse_command(char *input, char *argv[], int max_args)
{
    int argc = 0;
    char *p = input;

    while (*p != '\0')
    {
        /* Skip whitespace */
        while (isspace((unsigned char)*p))
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        if (argc >= max_args)
        {
            return -1;
        }

        /*
         * Quoted argument
         *
         * Example:
         * "Hello World"
         */
        if (*p == '"')
        {
            p++;

            argv[argc++] = p;

            char *out = p;

            while (*p != '\0' && *p != '"')
            {
                *out = *p;
                out++;
                p++;
            }

            /*
             * Opening quote existed but closing quote
             * was never found.
             */
            if (*p != '"')
            {
                return -1;
            }

            *out = '\0';

            /* Skip closing quote */
            p++;
        }

        /*
         * Normal argument
         */
        else
        {
            argv[argc++] = p;

            while (*p != '\0' &&
                   !isspace((unsigned char)*p))
            {
                p++;
            }

            if (*p != '\0')
            {
                *p = '\0';
                p++;
            }
        }
    }

    return argc;
}


/**
 * Find an Entry directly.
 *
 * We need this because GET and TTL need access to
 * the expiration timestamp.
 */
static Entry *find_entry(HashTable *table, const char *key)
{
    if (table == NULL || key == NULL)
    {
        return NULL;
    }

    size_t index = hash_key(key) % table->size;

    Entry *entry = table->buckets[index];

    while (entry != NULL)
    {
        if (strcmp(entry->key, key) == 0)
        {
            return entry;
        }

        entry = entry->next;
    }

    return NULL;
}


/**
 * Parse a positive integer used for EX seconds.
 */
static int parse_expiry_seconds(
    const char *value,
    long long *seconds
)
{
    if (value == NULL || seconds == NULL || *value == '\0')
    {
        return 0;
    }

    char *end;

    long long parsed = strtoll(value, &end, 10);

    /*
     * Entire argument must be a valid integer.
     */
    if (*end != '\0')
    {
        return 0;
    }

    /*
     * EX must be positive.
     */
    if (parsed <= 0)
    {
        return 0;
    }

    *seconds = parsed;

    return 1;
}


/**
 * Execute one database command.
 *
 * Return values:
 *
 * 0 = command executed normally
 * 1 = EXIT requested
 * -1 = error
 */
int command_execute(
    HashTable *table,
    char *input,
    char *response,
    size_t response_size
)
{
    char *argv[MAX_ARGS];

    int argc = parse_command(
        input,
        argv,
        MAX_ARGS
    );

    if (argc == -1)
    {
        snprintf(
            response,
            response_size,
            "ERR invalid command\n"
        );

        return -1;
    }

    if (argc == 0)
    {
        response[0] = '\0';
        return 0;
    }


    /*
     * Make command name case-insensitive.
     *
     * set -> SET
     * Set -> SET
     * SET -> SET
     */
    for (char *p = argv[0]; *p != '\0'; p++)
    {
        *p = (char)toupper((unsigned char)*p);
    }


    /* 
     * SET
     */
    if (strcmp(argv[0], "SET") == 0)
    {
        /*
         * Supported:
         *
         * SET key value
         *
         * SET key value EX seconds
         */
        if (argc != 3 && argc != 5)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n"
            );

            return -1;
        }

        long long expiry_seconds = 0;
        int has_expiry = 0;

        /*
         * SET key value EX seconds
         */
        if (argc == 5)
        {
            /*
             * Make EX case-insensitive.
             */
            for (char *p = argv[3]; *p != '\0'; p++)
            {
                *p = (char)toupper((unsigned char)*p);
            }

            if (strcmp(argv[3], "EX") != 0)
            {
                snprintf(
                    response,
                    response_size,
                    "ERR syntax error\n"
                );

                return -1;
            }

            if (!parse_expiry_seconds(
                    argv[4],
                    &expiry_seconds))
            {
                snprintf(
                    response,
                    response_size,
                    "ERR invalid expire time\n"
                );

                return -1;
            }

            has_expiry = 1;
        }


        /*
         * Persist first.
         *
         * BUG FIX: Previously, expiry_seconds (a relative
         * duration) was passed directly to
         * persistence_append_set_expiry(), which expects an
         * absolute Unix timestamp (EXAT). This caused all
         * TTL keys to appear already-expired (timestamp ~10
         * = 1970-01-01 00:00:10) on every server restart.
         *
         * Fix: convert relative seconds -> absolute timestamp
         * before the AOF write, and use the SAME value for
         * the in-memory entry so both are always consistent.
         */
        int persisted;

        /* Absolute expiration timestamp (0 = no expiry). */
        time_t expires_at = 0;

        if (has_expiry)
        {
            expires_at = time(NULL) + (time_t)expiry_seconds;

            persisted = persistence_append_set_expiry(
                argv[1],
                argv[2],
                expires_at
            );
        }
        else
        {
            persisted = persistence_append_set(
                argv[1],
                argv[2]
            );
        }

        if (!persisted)
        {
            snprintf(
                response,
                response_size,
                "ERR failed to write AOF\n"
            );

            return -1;
        }


        /*
         * Modify in-memory database only after
         * persistence succeeds.
         */
        if (!hash_table_set(
                table,
                argv[1],
                argv[2]))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to set key\n"
            );

            return -1;
        }


        /*
         * Apply expiration to the newly created/updated entry.
         *
         * We use the same absolute timestamp (expires_at) that
         * was written to the AOF, ensuring memory and disk are
         * always consistent.
         */
        if (has_expiry)
        {
            Entry *entry = find_entry(
                table,
                argv[1]
            );

            if (entry == NULL)
            {
                snprintf(
                    response,
                    response_size,
                    "ERR failed to set expiration\n"
                );

                return -1;
            }

            /* Set absolute timestamp directly (already computed above). */
            entry->expires_at = expires_at;
        }
        else
        {
            /*
             * Normal SET removes any previous expiration.
             *
             * Example:
             *
             * SET key value EX 10
             * SET key newvalue
             *
             * The second SET makes the key persistent.
             */
            Entry *entry = find_entry(
                table,
                argv[1]
            );

            if (entry != NULL)
            {
                entry->expires_at = 0;
            }
        }

        snprintf(
            response,
            response_size,
            "OK\n"
        );

        return 0;
    }


    /* 
     * GET
     */
    else if (strcmp(argv[0], "GET") == 0)
    {
        if (argc != 2)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n"
            );

            return -1;
        }

        Entry *entry = find_entry(
            table,
            argv[1]
        );

        if (entry == NULL)
        {
            snprintf(
                response,
                response_size,
                "(nil)\n"
            );

            return 0;
        }

        /*
         * Expired keys behave exactly like missing keys.
         */
        if (entry_is_expired(entry))
        {
            snprintf(
                response,
                response_size,
                "(nil)\n"
            );

            return 0;
        }

        snprintf(
            response,
            response_size,
            "%s\n",
            entry->value
        );

        return 0;
    }


    /* 
     * TTL
     */
    else if (strcmp(argv[0], "TTL") == 0)
    {
        if (argc != 2)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n"
            );

            return -1;
        }

        Entry *entry = find_entry(
            table,
            argv[1]
        );

        /*
         * Key doesn't exist.
         *
         * Redis-style:
         * -2 = key doesn't exist
         */
        if (entry == NULL)
        {
            snprintf(
                response,
                response_size,
                "-2\n"
            );

            return 0;
        }

        /*
         * Key exists but has expired.
         */
        if (entry_is_expired(entry))
        {
            snprintf(
                response,
                response_size,
                "-2\n"
            );

            return 0;
        }

        /*
         * No expiration.
         *
         * Redis-style:
         * -1 = key exists but has no expiry
         */
        if (entry->expires_at == 0)
        {
            snprintf(
                response,
                response_size,
                "-1\n"
            );

            return 0;
        }

        time_t now = time(NULL);

        long long remaining =
            (long long)(entry->expires_at - now);

        /*
         * Protect against clock-boundary edge case.
         */
        if (remaining <= 0)
        {
            snprintf(
                response,
                response_size,
                "-2\n"
            );

            return 0;
        }

        snprintf(
            response,
            response_size,
            "%lld\n",
            remaining
        );

        return 0;
    }


    /* 
     * DEL
     */
    else if (strcmp(argv[0], "DEL") == 0)
    {
        if (argc != 2)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n"
            );

            return -1;
        }

        /*
         * Check whether key exists.
         */
        Entry *entry = find_entry(
            table,
            argv[1]
        );

        if (entry == NULL ||
            entry_is_expired(entry))
        {
            snprintf(
                response,
                response_size,
                "0\n"
            );

            return 0;
        }

        /*
         * Persist first.
         */
        if (!persistence_append_del(argv[1]))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to write AOF\n"
            );

            return -1;
        }

        /*
         * Delete from memory only after
         * persistence succeeds.
         */
        if (!hash_table_delete(
                table,
                argv[1]))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to delete key\n"
            );

            return -1;
        }

        snprintf(
            response,
            response_size,
            "1\n"
        );

        return 0;
    }


    /*
     * PING
     */
    else if (strcmp(argv[0], "PING") == 0)
    {
        if (argc != 1)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n"
            );

            return -1;
        }

        snprintf(
            response,
            response_size,
            "PONG\n"
        );

        return 0;
    }


    /* 
     * EXIT
     */
    else if (strcmp(argv[0], "EXIT") == 0)
    {
        if (argc != 1)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n"
            );

            return -1;
        }

        if (!persistence_rewrite(table))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to rewrite AOF\n"
            );

            return -1;
        }

        snprintf(
            response,
            response_size,
            "BYE\n"
        );

        return 1;
    }


    /* 
     * UNKNOWN
     */
    else
    {
        snprintf(
            response,
            response_size,
            "ERR unknown command\n"
        );

        return -1;
    }
}


/**
 * Local CLI loop.
 */
void command_loop(HashTable *table)
{
    char input[INPUT_SIZE];
    char response[INPUT_SIZE];

    while (1)
    {
        printf("InMemDB> ");

        if (fgets(
                input,
                sizeof(input),
                stdin) == NULL)
        {
            break;
        }

        /*
         * Remove newline added by fgets().
         */
        input[strcspn(
            input,
            "\n"
        )] = '\0';

        int result = command_execute(
            table,
            input,
            response,
            sizeof(response)
        );

        if (response[0] != '\0')
        {
            printf("%s", response);
        }

        if (result == 1)
        {
            break;
        }
    }

    return;
}