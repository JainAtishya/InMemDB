#ifndef COMMAND_H
#define COMMAND_H

#include <stddef.h>

#include "hash_table.h"

int parse_command(
    char *input,
    char *argv[],
    int max_args
);

int command_execute(
    HashTable *table,
    char *input,
    char *response,
    size_t response_size
);

void command_loop(
    HashTable *table
);

#endif