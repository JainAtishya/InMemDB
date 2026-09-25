#ifndef ENTRY_H
#define ENTRY_H

#include <time.h>

typedef struct Entry
{
    char *key;
    char *value;

    /*
     * Unix timestamp in seconds.
     *
     * 0 means the key does not expire.
     */
    time_t expires_at;

    struct Entry *next;

} Entry;

Entry *entry_create(
    const char *key,
    const char *value
);

void entry_destroy(
    Entry *entry
);

/*
 * Set expiration time in seconds from now.
 *
 * Example:
 *
 * entry_set_expiry(entry, 10);
 *
 * means the entry expires approximately
 * 10 seconds from now.
 */
void entry_set_expiry(
    Entry *entry,
    long long seconds
);

/*
 * Set an absolute expiration timestamp.
 *
 * This is mainly used by persistence so that
 * expiration survives server restarts correctly.
 */
void entry_set_expiry_at(
    Entry *entry,
    time_t expires_at
);

/*
 * Returns:
 *
 * 1 -> entry has expired
 * 0 -> entry is still valid / has no expiration
 */
int entry_is_expired(
    const Entry *entry
);

#endif