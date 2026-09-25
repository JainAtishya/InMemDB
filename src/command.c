#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "command.h"
#include "persistence.h"

#define INPUT_SIZE 256
#define MAX_ARGS 5

int parse_command(
    char *input,
    char *argv[],
    int max_args
)
{
    int argc = 0;

    char *p = input;

    while (*p != '\0')
    {
        /*
         * Skip whitespace.
         */
        while (
            isspace(
                (unsigned char)*p
            )
        )
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
         * Quoted argument.
         *
         * Example:
         *
         * SET message "Hello World"
         */
        if (*p == '"')
        {
            p++;

            argv[argc++] = p;

            char *out = p;

            while (
                *p != '\0' &&
                *p != '"'
            )
            {
                *out = *p;

                out++;
                p++;
            }

            /*
             * Opening quote existed but
             * closing quote was not found.
             */
            if (*p != '"')
            {
                return -1;
            }

            *out = '\0';

            /*
             * Skip closing quote.
             */
            p++;
        }

        /*
         * Normal argument.
         */
        else
        {
            argv[argc++] = p;

            while (
                *p != '\0' &&
                !isspace(
                    (unsigned char)*p
                )
            )
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

/*
 * Parse positive integer seconds.
 *
 * Returns:
 *
 * 1 -> valid
 * 0 -> invalid
 */
static int parse_expiry_seconds(
    const char *text,
    long long *seconds
)
{
    if (
        text == NULL ||
        seconds == NULL ||
        *text == '\0'
    )
    {
        return 0;
    }

    errno = 0;

    char *end;

    long long value =
        strtoll(
            text,
            &end,
            10
        );

    if (
        errno == ERANGE ||
        end == text ||
        *end != '\0'
    )
    {
        return 0;
    }

    /*
     * TTL must be positive.
     */
    if (value <= 0)
    {
        return 0;
    }

    *seconds = value;

    return 1;
}

int command_execute(
    HashTable *table,
    char *input,
    char *response,
    size_t response_size
)
{
    char *argv[MAX_ARGS];

    int argc =
        parse_command(
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
    for (
        char *p = argv[0];
        *p != '\0';
        p++
    )
    {
        *p =
            (char)toupper(
                (unsigned char)*p
            );
    }

    /*
     * ================= SET =================
     *
     * Supported:
     *
     * SET key value
     *
     * SET key value EX seconds
     */
    if (strcmp(argv[0], "SET") == 0)
    {
        /*
         * Normal SET.
         */
        if (argc == 3)
        {
            /*
             * Persist first.
             *
             * No expiration.
             */
            if (
                !persistence_append_set(
                    argv[1],
                    argv[2]
                )
            )
            {
                snprintf(
                    response,
                    response_size,
                    "ERR failed to write AOF\n"
                );

                return -1;
            }

            /*
             * Modify memory only after
             * persistence succeeds.
             */
            if (
                !hash_table_set_with_expiry(
                    table,
                    argv[1],
                    argv[2],
                    0
                )
            )
            {
                snprintf(
                    response,
                    response_size,
                    "ERR failed to set key\n"
                );

                return -1;
            }

            snprintf(
                response,
                response_size,
                "OK\n"
            );

            return 0;
        }

        /*
         * SET with expiration:
         *
         * SET key value EX seconds
         */
        if (argc == 5)
        {
            /*
             * Only EX is supported for now.
             */
            for (
                char *p = argv[3];
                *p != '\0';
                p++
            )
            {
                *p =
                    (char)toupper(
                        (unsigned char)*p
                    );
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

            long long seconds;

            if (
                !parse_expiry_seconds(
                    argv[4],
                    &seconds
                )
            )
            {
                snprintf(
                    response,
                    response_size,
                    "ERR invalid expire time\n"
                );

                return -1;
            }

            /*
             * Calculate the absolute expiration
             * timestamp ONCE.
             *
             * This exact timestamp is used both
             * for persistence and memory.
             */
            time_t expires_at =
                time(NULL) +
                (time_t)seconds;

            /*
             * Persist first.
             */
            if (
                !persistence_append_set_expiry(
                    argv[1],
                    argv[2],
                    expires_at
                )
            )
            {
                snprintf(
                    response,
                    response_size,
                    "ERR failed to write AOF\n"
                );

                return -1;
            }

            /*
             * Now update memory.
             */
            if (
                !hash_table_set_with_expiry(
                    table,
                    argv[1],
                    argv[2],
                    expires_at
                )
            )
            {
                snprintf(
                    response,
                    response_size,
                    "ERR failed to set key\n"
                );

                return -1;
            }

            snprintf(
                response,
                response_size,
                "OK\n"
            );

            return 0;
        }

        snprintf(
            response,
            response_size,
            "ERR wrong number of arguments\n"
        );

        return -1;
    }

    /*
     * ================= GET =================
     */
    else if (
        strcmp(argv[0], "GET") == 0
    )
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

        const char *value =
            hash_table_get(
                table,
                argv[1]
            );

        if (value == NULL)
        {
            snprintf(
                response,
                response_size,
                "(nil)\n"
            );
        }
        else
        {
            snprintf(
                response,
                response_size,
                "%s\n",
                value
            );
        }

        return 0;
    }

    /*
     * ================= DEL =================
     */
    else if (
        strcmp(argv[0], "DEL") == 0
    )
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
         * If key does not exist or has expired,
         * GET returns NULL.
         */
        if (
            hash_table_get(
                table,
                argv[1]
            ) == NULL
        )
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
        if (
            !persistence_append_del(
                argv[1]
            )
        )
        {
            snprintf(
                response,
                response_size,
                "ERR failed to write AOF\n"
            );

            return -1;
        }

        /*
         * Delete from memory.
         */
        if (
            !hash_table_delete(
                table,
                argv[1]
            )
        )
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
     * ================= PING =================
     */
    else if (
        strcmp(argv[0], "PING") == 0
    )
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
     * ================= EXIT =================
     */
    else if (
        strcmp(argv[0], "EXIT") == 0
    )
    {
        if (
            !persistence_rewrite(table)
        )
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
     * ================= UNKNOWN =================
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

void command_loop(
    HashTable *table
)
{
    char input[INPUT_SIZE];

    char response[INPUT_SIZE];

    while (1)
    {
        printf("InMemDB> ");

        if (
            fgets(
                input,
                sizeof(input),
                stdin
            ) == NULL
        )
        {
            break;
        }

        /*
         * Remove newline added by fgets().
         */
        input[
            strcspn(
                input,
                "\n"
            )
        ] = '\0';

        int result =
            command_execute(
                table,
                input,
                response,
                sizeof(response)
            );

        if (response[0] != '\0')
        {
            printf(
                "%s",
                response
            );
        }

        /*
         * EXIT requested.
         */
        if (result == 1)
        {
            break;
        }
    }
}