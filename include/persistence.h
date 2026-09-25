#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <time.h>

#include "hash_table.h"

int persistence_append_set(
    const char *key,
    const char *value
);

int persistence_append_set_expiry(
    const char *key,
    const char *value,
    time_t expires_at
);

int persistence_append_del(
    const char *key
);

int persistence_load(
    HashTable *table
);

int persistence_rewrite(
    HashTable *table
);

#endif