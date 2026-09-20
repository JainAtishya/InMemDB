#include <stdlib.h>
#include <string.h>

#include "entry.h"

Entry *entry_create(const char *key, const char *value)
{
    Entry *entry = malloc(sizeof(Entry));

    if (entry == NULL)
    {
        return NULL;
    }

    entry->key = malloc(strlen(key) + 1);

    if (entry->key == NULL)
    {
        free(entry);
        return NULL;
    }

    strcpy(entry->key, key);

    entry->value = malloc(strlen(value) + 1);

    if (entry->value == NULL)
    {
        free(entry->key);
        free(entry);
        return NULL;
    }

    strcpy(entry->value, value);

    entry->next = NULL;

    return entry;
}

void entry_destroy(Entry *entry)
{
    if (entry == NULL)
    {
        return;
    }

    free(entry->key);
    free(entry->value);
    free(entry);
}