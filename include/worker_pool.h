#ifndef WORKER_POOL_H
#define WORKER_POOL_H

#include <pthread.h>
#include "hash_table.h"

#define WORKER_COUNT 4
#define MAX_COMMAND_SIZE 1024

typedef struct Task
{
    int client_fd;
    char command[MAX_COMMAND_SIZE];
    struct Task *next;
} Task;

typedef struct
{
    pthread_t workers[WORKER_COUNT];

    Task *head;
    Task *tail;

    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;

    pthread_rwlock_t db_lock;

    HashTable *table;

    int notify_pipe[2];

    int shutting_down;

} WorkerPool;

int worker_pool_init(
    WorkerPool *pool,
    HashTable *table
);

int worker_pool_submit(
    WorkerPool *pool,
    int client_fd,
    const char *command
);

int worker_pool_get_notify_fd(
    WorkerPool *pool
);

void worker_pool_destroy(
    WorkerPool *pool
);

#endif