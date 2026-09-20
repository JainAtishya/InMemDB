#ifndef ENTRY_H
#define ENTRY_H

typedef struct Entry {
    char *key;
    char *value;
    struct Entry *next;
} Entry;

Entry *entry_create(const char *key, const char *value);
void entry_destroy(Entry *entry);

#endif