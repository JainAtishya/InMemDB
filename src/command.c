#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "command.h"

#define INPUT_SIZE 256
#define MAX_ARGS 4


int parse_command(char *input, char *argv[], int max_args)
{
    int argc = 0;
    char *p = input;

    while (*p != '\0')
    {
        /* Skip spaces */
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

        /* Quoted argument */
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

            if (*p != '"')
            {
                return -1;
            }

            *out = '\0';
            p++;
        }

        /* Normal argument */
        else
        {
            argv[argc++] = p;

            while (*p != '\0' && !isspace((unsigned char)*p))
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


void command_loop(HashTable *table)
{
    char input[INPUT_SIZE];
    char *argv[MAX_ARGS];

    while (1)
    {
        printf("InMemDB> ");

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        int argc = parse_command(input, argv, MAX_ARGS);

        if (argc == -1)
        {
            printf("ERR invalid command\n");
            continue;
        }

        if (argc == 0)
        {
            continue;
        }

        /* Make command case-insensitive */
        for (char *p = argv[0]; *p != '\0'; p++)
        {
            *p = (char)toupper((unsigned char)*p);
        }

        /* ================= SET ================= */

        if (strcmp(argv[0], "SET") == 0)
        {
            if (argc != 3)
            {
                printf("ERR wrong number of arguments\n");
                continue;
            }

            if (hash_table_set(table, argv[1], argv[2]))
            {
                printf("OK\n");
            }
            else
            {
                printf("ERR failed to set key\n");
            }
        }

        /* ================= GET ================= */

        else if (strcmp(argv[0], "GET") == 0)
        {
            if (argc != 2)
            {
                printf("ERR wrong number of arguments\n");
                continue;
            }

            const char *value = hash_table_get(table, argv[1]);

            if (value == NULL)
            {
                printf("(nil)\n");
            }
            else
            {
                printf("%s\n", value);
            }
        }

        /* ================= DEL ================= */

        else if (strcmp(argv[0], "DEL") == 0)
        {
            if (argc != 2)
            {
                printf("ERR wrong number of arguments\n");
                continue;
            }

            if (hash_table_delete(table, argv[1]))
            {
                printf("1\n");
            }
            else
            {
                printf("0\n");
            }
        }

        /* ================= EXIT ================= */

        else if (strcmp(argv[0], "EXIT") == 0)
        {
            break;
        }

        else
        {
            printf("ERR unknown command\n");
        }
    }
}