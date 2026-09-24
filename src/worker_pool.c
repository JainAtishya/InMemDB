#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

#include "worker_pool.h"
#include "command.h"

static Task *task_pop(WorkerPool *pool)
{
    Task *task = pool->head;

    if (task == NULL)
    {
        return NULL;
    }

    pool->head = task->next;

    if (pool->head == NULL)
    {
        pool->tail = NULL;
    }

    task->next = NULL;

    return task;
}

static int get_worker_id(WorkerPool *pool)
{
    pthread_t current_thread = pthread_self();

    for (int i = 0; i < WORKER_COUNT; i++)
    {
        if (pthread_equal(current_thread, pool->workers[i]))
        {
            return i;
        }
    }

    return -1;
}

static int is_read_command(const char *command)
{
    while (isspace((unsigned char)*command))
    {
        command++;
    }

    if (strncasecmp(command, "get", 3) != 0)
    {
        return 0;
    }

    command += 3;

    return *command == '\0' || isspace((unsigned char)*command);
}

static void *worker_main(void *arg)
{
    WorkerPool *pool = arg;
    int worker_id = get_worker_id(pool);

    printf("[Worker %d] started\n", worker_id);

    while (1)
    {
        pthread_mutex_lock(&pool->queue_mutex);

        while (pool->head == NULL && !pool->shutting_down)
        {
            pthread_cond_wait(
                &pool->queue_cond,
                &pool->queue_mutex
            );
        }

        if (pool->head == NULL && pool->shutting_down)
        {
            pthread_mutex_unlock(&pool->queue_mutex);

            printf(
                "[Worker %d] shutting down\n",
                worker_id
            );

            break;
        }

        Task *task = task_pop(pool);

        pthread_mutex_unlock(&pool->queue_mutex);

        if (task == NULL)
        {
            continue;
        }

        printf(
            "[Worker %d] picked task for client FD %d: %s\n",
            worker_id,
            task->client_fd,
            task->command
        );

        char response[MAX_COMMAND_SIZE];
        response[0] = '\0';

        int result;

        if (is_read_command(task->command))
        {
            printf(
                "[Worker %d] acquiring read lock\n",
                worker_id
            );

            pthread_rwlock_rdlock(&pool->db_lock);

            printf(
                "[Worker %d] executing GET for client FD %d\n",
                worker_id,
                task->client_fd
            );

            result = command_execute(
                pool->table,
                task->command,
                response,
                sizeof(response)
            );

            pthread_rwlock_unlock(&pool->db_lock);

            printf(
                "[Worker %d] released read lock\n",
                worker_id
            );
        }
        else
        {
            printf(
                "[Worker %d] acquiring write lock\n",
                worker_id
            );

            pthread_rwlock_wrlock(&pool->db_lock);

            printf(
                "[Worker %d] executing write command for client FD %d\n",
                worker_id,
                task->client_fd
            );

            result = command_execute(
                pool->table,
                task->command,
                response,
                sizeof(response)
            );

            pthread_rwlock_unlock(&pool->db_lock);

            printf(
                "[Worker %d] released write lock\n",
                worker_id
            );
        }

        if (response[0] != '\0')
        {
            ssize_t sent = send(
                task->client_fd,
                response,
                strlen(response),
                MSG_NOSIGNAL
            );

            if (sent == -1)
            {
                perror("send");
            }
            else
            {
                printf(
                    "[Worker %d] response sent to client FD %d\n",
                    worker_id,
                    task->client_fd
                );
            }
        }

        if (result == 1)
        {
            printf(
                "[Worker %d] client FD %d requested close\n",
                worker_id,
                task->client_fd
            );

            ssize_t written = write(
                pool->notify_pipe[1],
                &task->client_fd,
                sizeof(task->client_fd)
            );

            if (written != sizeof(task->client_fd))
            {
                perror("write notify pipe");
            }
        }

        free(task);
    }

    return NULL;
}

int worker_pool_init(
    WorkerPool *pool,
    HashTable *table
)
{
    if (pool == NULL || table == NULL)
    {
        return 0;
    }

    memset(pool, 0, sizeof(*pool));

    pool->table = table;

    if (pipe(pool->notify_pipe) == -1)
    {
        perror("pipe");
        return 0;
    }

    if (pthread_mutex_init(
            &pool->queue_mutex,
            NULL
        ) != 0)
    {
        close(pool->notify_pipe[0]);
        close(pool->notify_pipe[1]);

        return 0;
    }

    if (pthread_cond_init(
            &pool->queue_cond,
            NULL
        ) != 0)
    {
        pthread_mutex_destroy(
            &pool->queue_mutex
        );

        close(pool->notify_pipe[0]);
        close(pool->notify_pipe[1]);

        return 0;
    }

    if (pthread_rwlock_init(
            &pool->db_lock,
            NULL
        ) != 0)
    {
        pthread_cond_destroy(
            &pool->queue_cond
        );

        pthread_mutex_destroy(
            &pool->queue_mutex
        );

        close(pool->notify_pipe[0]);
        close(pool->notify_pipe[1]);

        return 0;
    }

    pool->shutting_down = 0;

    for (int i = 0; i < WORKER_COUNT; i++)
    {
        if (pthread_create(
                &pool->workers[i],
                NULL,
                worker_main,
                pool
            ) != 0)
        {
            fprintf(
                stderr,
                "Failed to create worker thread\n"
            );

            pthread_mutex_lock(
                &pool->queue_mutex
            );

            pool->shutting_down = 1;

            pthread_cond_broadcast(
                &pool->queue_cond
            );

            pthread_mutex_unlock(
                &pool->queue_mutex
            );

            for (int j = 0; j < i; j++)
            {
                pthread_join(
                    pool->workers[j],
                    NULL
                );
            }

            pthread_rwlock_destroy(
                &pool->db_lock
            );

            pthread_cond_destroy(
                &pool->queue_cond
            );

            pthread_mutex_destroy(
                &pool->queue_mutex
            );

            close(pool->notify_pipe[0]);
            close(pool->notify_pipe[1]);

            return 0;
        }
    }

    printf(
        "Worker pool started with %d workers.\n",
        WORKER_COUNT
    );

    return 1;
}

int worker_pool_submit(
    WorkerPool *pool,
    int client_fd,
    const char *command
)
{
    if (pool == NULL || command == NULL)
    {
        return 0;
    }

    Task *task = malloc(sizeof(Task));

    if (task == NULL)
    {
        return 0;
    }

    task->client_fd = client_fd;

    strncpy(
        task->command,
        command,
        MAX_COMMAND_SIZE - 1
    );

    task->command[MAX_COMMAND_SIZE - 1] = '\0';

    task->next = NULL;

    pthread_mutex_lock(
        &pool->queue_mutex
    );

    if (pool->shutting_down)
    {
        pthread_mutex_unlock(
            &pool->queue_mutex
        );

        free(task);

        return 0;
    }

    if (pool->tail == NULL)
    {
        pool->head = task;
        pool->tail = task;
    }
    else
    {
        pool->tail->next = task;
        pool->tail = task;
    }

    printf(
        "[Queue] Task added for client FD %d: %s\n",
        client_fd,
        command
    );

    pthread_cond_signal(
        &pool->queue_cond
    );

    pthread_mutex_unlock(
        &pool->queue_mutex
    );

    return 1;
}

int worker_pool_get_notify_fd(
    WorkerPool *pool
)
{
    if (pool == NULL)
    {
        return -1;
    }

    return pool->notify_pipe[0];
}

void worker_pool_destroy(
    WorkerPool *pool
)
{
    if (pool == NULL)
    {
        return;
    }

    pthread_mutex_lock(
        &pool->queue_mutex
    );

    pool->shutting_down = 1;

    pthread_cond_broadcast(
        &pool->queue_cond
    );

    pthread_mutex_unlock(
        &pool->queue_mutex
    );

    for (int i = 0; i < WORKER_COUNT; i++)
    {
        pthread_join(
            pool->workers[i],
            NULL
        );
    }

    pthread_mutex_lock(
        &pool->queue_mutex
    );

    Task *current = pool->head;

    while (current != NULL)
    {
        Task *next = current->next;

        free(current);

        current = next;
    }

    pool->head = NULL;
    pool->tail = NULL;

    pthread_mutex_unlock(
        &pool->queue_mutex
    );

    pthread_rwlock_destroy(
        &pool->db_lock
    );

    pthread_cond_destroy(
        &pool->queue_cond
    );

    pthread_mutex_destroy(
        &pool->queue_mutex
    );

    close(pool->notify_pipe[0]);
    close(pool->notify_pipe[1]);

    printf("Worker pool stopped.\n");
}