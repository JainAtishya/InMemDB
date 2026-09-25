#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "entry.h"

Entry *entry_create(
    const char *key,
    const char *value
)
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

    /*
     * 0 means:
     * this entry has no expiration.
     */
    entry->expires_at = 0;

    entry->next = NULL;

    return entry;
}

void entry_destroy(
    Entry *entry
)
{
    if (entry == NULL)
    {
        return;
    }

    free(entry->key);
    free(entry->value);
    free(entry);
}

void entry_set_expiry(
    Entry *entry,
    long long seconds
)
{
    if (entry == NULL)
    {
        return;
    }

    /*
     * Non-positive values mean:
     * remove expiration.
     */
    if (seconds <= 0)
    {
        entry->expires_at = 0;
        return;
    }

    entry->expires_at =
        time(NULL) + (time_t)seconds;
}

void entry_set_expiry_at(
    Entry *entry,
    time_t expires_at
)
{
    if (entry == NULL)
    {
        return;
    }

    entry->expires_at = expires_at;
}

int entry_is_expired(
    const Entry *entry
)
{
    if (entry == NULL)
    {
        return 0;
    }

    /*
     * expires_at == 0 means
     * no expiration.
     */
    if (entry->expires_at == 0)
    {
        return 0;
    }

    return time(NULL) >= entry->expires_at;
}