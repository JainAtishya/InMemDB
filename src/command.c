#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "command.h"
#include "persistence.h"

#define INPUT_SIZE 256
#define MAX_ARGS 4


/*
 * Parse a command string into argv-style arguments.
 *
 * Example:
 *
 * SET name Atishya
 *
 * becomes:
 *
 * argv[0] = "SET"
 * argv[1] = "name"
 * argv[2] = "Atishya"
 *
 * Quoted arguments are supported:
 *
 * SET message "Hello World"
 *
 * becomes:
 *
 * argv[0] = "SET"
 * argv[1] = "message"
 * argv[2] = "Hello World"
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

            /*
             * Skip closing quote.
             */
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


/*
 * Execute one database command.
 *
 * This function is the important part of the networking refactor.
 *
 * The function does NOT:
 *
 * - read from stdin
 * - print to stdout
 * - call recv()
 * - call send()
 *
 * Instead:
 *
 * input    -> command received from CLI/socket
 * response -> response generated for CLI/socket
 *
 * Return value:
 *
 *  0 = command executed normally
 *  1 = EXIT requested
 * -1 = error
 */
int command_execute(
    HashTable *table,
    char *input,
    char *response,
    size_t response_size)
{
    char *argv[MAX_ARGS];

    int argc = parse_command(input, argv, MAX_ARGS);

    if (argc == -1)
    {
        snprintf(
            response,
            response_size,
            "ERR invalid command\n");

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


    /* ================= SET ================= */

    if (strcmp(argv[0], "SET") == 0)
    {
        if (argc != 3)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n");

            return -1;
        }

        /*
         * Write to AOF FIRST.
         *
         * This guarantees that if persistence fails,
         * we do not modify the in-memory database.
         */
        if (!persistence_append_set(argv[1], argv[2]))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to write AOF\n");

            return -1;
        }

        /*
         * Only modify HashTable after persistence succeeds.
         */
        if (!hash_table_set(table, argv[1], argv[2]))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to set key\n");

            return -1;
        }

        snprintf(
            response,
            response_size,
            "OK\n");

        return 0;
    }


    /* ================= GET ================= */

    else if (strcmp(argv[0], "GET") == 0)
    {
        if (argc != 2)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n");

            return -1;
        }

        const char *value =
            hash_table_get(table, argv[1]);

        if (value == NULL)
        {
            snprintf(
                response,
                response_size,
                "(nil)\n");
        }
        else
        {
            snprintf(
                response,
                response_size,
                "%s\n",
                value);
        }

        return 0;
    }


    /* ================= DEL ================= */

    else if (strcmp(argv[0], "DEL") == 0)
    {
        if (argc != 2)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n");

            return -1;
        }

        /*
         * Check whether the key exists BEFORE
         * writing DEL to the AOF.
         */
        if (hash_table_get(table, argv[1]) == NULL)
        {
            snprintf(
                response,
                response_size,
                "0\n");

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
                "ERR failed to write AOF\n");

            return -1;
        }

        /*
         * Delete from memory only after persistence succeeds.
         */
        if (!hash_table_delete(table, argv[1]))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to delete key\n");

            return -1;
        }

        snprintf(
            response,
            response_size,
            "1\n");

        return 0;
    }


    /* ================= PING ================= */

    else if (strcmp(argv[0], "PING") == 0)
    {
        if (argc != 1)
        {
            snprintf(
                response,
                response_size,
                "ERR wrong number of arguments\n");

            return -1;
        }

        snprintf(
            response,
            response_size,
            "PONG\n");

        return 0;
    }


    /* ================= EXIT ================= */

    else if (strcmp(argv[0], "EXIT") == 0)
    {
        /*
         * EXIT is mainly useful for the CLI.
         *
         * The caller decides what to do with the
         * EXIT request.
         */
        if (!persistence_rewrite(table))
        {
            snprintf(
                response,
                response_size,
                "ERR failed to rewrite AOF\n");

            return -1;
        }

        snprintf(
            response,
            response_size,
            "BYE\n");

        return 1;
    }


    /* ================= UNKNOWN ================= */

    else
    {
        snprintf(
            response,
            response_size,
            "ERR unknown command\n");

        return -1;
    }
}


/*
 * Local CLI loop.
 *
 * This is now just a frontend around command_execute().
 *
 * The networking layer will NOT use this function.
 */
void command_loop(HashTable *table)
{
    char input[INPUT_SIZE];

    char response[INPUT_SIZE];

    while (1)
    {
        printf("InMemDB> ");

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

        /*
         * Remove newline added by fgets().
         */
        input[strcspn(input, "\n")] = '\0';

        int result =
            command_execute(
                table,
                input,
                response,
                sizeof(response));

        /*
         * Print whatever command_execute()
         * generated.
         */
        if (response[0] != '\0')
        {
            printf("%s", response);
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