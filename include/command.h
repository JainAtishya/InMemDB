#ifndef COMMAND_H
#define COMMAND_H

#include "hash_table.h"

int parse_command(char *input, char *argv[], int max_args);
void command_loop(HashTable *table);

#endif