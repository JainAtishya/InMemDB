# InMemDB

An in-memory key-value database written in C.

InMemDB is a TCP server that stores key-value pairs in a hash table, supports key expiration (TTL), persists data to disk using an append-only file, and handles multiple clients concurrently using a thread pool.

## Table of Contents

- [Overview](#overview)
- [Why I Built This](#why-i-built-this)
- [Features](#features)
- [Architecture](#architecture)
- [Request Flow](#request-flow)
- [Data Storage](#data-storage)
- [Hashing and Resizing](#hashing-and-resizing)
- [Commands](#commands)
- [TTL and Expiration](#ttl-and-expiration)
- [Persistence](#persistence)
- [Networking](#networking)
- [Concurrency](#concurrency)
- [Project Structure](#project-structure)
- [Building the Project](#building-the-project)
- [Running the Server](#running-the-server)
- [Cleaning](#cleaning)
- [Manual Usage](#manual-usage)
- [Design Decisions](#design-decisions)
- [Limitations](#limitations)
- [What This Project Demonstrates](#what-this-project-demonstrates)
- [Future Improvements](#future-improvements)
- [License](#license)

## Overview

InMemDB accepts client connections over TCP on `127.0.0.1:6379`. Clients send newline-delimited commands such as `SET`, `GET`, `DEL`, `TTL`, `PING`, and `EXIT`. The server processes these commands using a pool of worker threads and stores all data in a hash table that lives entirely in memory. Write operations are logged to an append-only file (`data.aof`) before modifying memory, so data can be recovered after a restart.

## Why I Built This

I built this project to understand how an in-memory database actually works under the hood. Instead of just using a database, I wanted to implement the internals myself: the hash table, the collision handling, the persistence layer, the network server, and the thread pool.

Every part of this project exists because I wanted to learn how it works by building it. The hash table taught me how data is stored and retrieved efficiently. The AOF taught me how databases survive crashes. The TCP server taught me how sockets and file descriptors work. The worker pool taught me how threads, mutexes, condition variables, and read-write locks fit together.

This is a learning project, not a production database.

## Features

- Hash table with separate chaining for collision resolution
- Automatic resizing when the load factor exceeds 0.75
- Six commands: `SET`, `GET`, `DEL`, `TTL`, `PING`, `EXIT`
- Key expiration using `SET key value EX seconds`
- Lazy expiration: expired keys are detected at access time, not by a background thread
- Append-only file (AOF) persistence with length-prefixed text format
- Persistence-first writes: every mutation is written to disk before modifying memory
- AOF compaction via rewrite with atomic file replacement
- Startup recovery: the AOF is replayed to rebuild the hash table, skipping already-expired keys
- TCP server using `select()` for I/O multiplexing
- Up to 16 concurrent client connections
- Worker thread pool with 4 threads
- Read-write locking: `GET` and `TTL` acquire a read lock (concurrent), all other commands acquire a write lock (exclusive)
- Notification pipe for communicating client disconnects from worker threads back to the server thread
- Case-insensitive command names and `EX` keyword
- Quoted string support in arguments

## Architecture

```
Client (e.g. netcat)
    |
    | TCP connection
    v
TCP Server (select loop)
    |
    | submit task
    v
Task Queue (linked list)
    |
    | wake worker
    v
Worker Thread (1 of 4)
    |
    | acquire read or write lock
    v
Command Execution
    |
    +-------+-------+
    |               |
    v               v
AOF Persistence   Hash Table
(data.aof)        (in-memory)
```

The server thread handles all I/O multiplexing using `select()`. It accepts new connections, receives data from clients, and extracts newline-delimited commands. Each complete command is wrapped in a task and pushed onto a shared queue. Worker threads pull tasks from the queue, determine whether the command is a read or write, acquire the appropriate lock on the database, execute the command, and send the response directly back to the client.

## Request Flow

Here is what happens internally when a client sends `SET name Atishya`:

1. The client sends `SET name Atishya\n` over TCP.
2. The server's `select()` loop detects data on the client's file descriptor.
3. `recv()` reads the bytes into a per-client buffer.
4. The server scans for a newline, extracts `SET name Atishya` as a complete command, and strips the `\n` (also handles `\r\n`).
5. `worker_pool_submit()` allocates a `Task` struct, copies the command string into it, appends the task to the tail of the queue, and signals the condition variable.
6. A sleeping worker thread wakes up, locks the queue mutex, pops the task from the head.
7. `is_read_command("SET name Atishya")` returns 0, so the worker acquires a write lock on the database (`pthread_rwlock_wrlock`).
8. `command_execute()` is called. Inside it:
   - `parse_command()` splits the input into `argv[0]="SET"`, `argv[1]="name"`, `argv[2]="Atishya"`.
   - The command name is uppercased (so `set`, `Set`, `SET` all work).
   - `persistence_append_set("name", "Atishya")` opens `data.aof` in append mode, writes `SET 4:name7:Atishya\n`, and closes the file.
   - Only after the AOF write succeeds, `hash_table_set(table, "name", "Atishya")` inserts the entry into the hash table.
   - Since there is no `EX` argument, any previous expiration on the key is cleared by setting `expires_at = 0`.
   - The response is set to `OK\n`.
9. The worker releases the write lock.
10. The worker sends `OK\n` back to the client via `send()`.
11. The task is freed.

## Data Storage

All data lives in a `HashTable` struct that contains:

- `Entry **buckets`: an array of pointers to `Entry`, one per bucket
- `size_t size`: the current number of buckets
- `size_t count`: the number of entries currently stored

Each `Entry` is a node in a singly linked list (for collision chaining) and contains:

- `char *key`: heap-allocated copy of the key string
- `char *value`: heap-allocated copy of the value string
- `time_t expires_at`: absolute Unix timestamp for expiration (0 means no expiration)
- `struct Entry *next`: pointer to the next entry in the same bucket

The hash table is created with an initial size of 8 buckets. As entries are inserted and the load factor exceeds the threshold, the table is resized automatically.

## Hashing and Resizing

**Hash function:** The hash is computed by iterating over each character in the key:

```
hash = 0
for each character c in key:
    hash = hash * 31 + c
```

This is a polynomial rolling hash with multiplier 31. The bucket index is then calculated as `hash % table_size`.

**Collision handling:** When two keys map to the same bucket, they form a linked list (separate chaining). New entries are prepended to the head of the chain.

**Load factor and resizing:** After every insertion, the load factor is calculated as `count / size`. If it exceeds 0.75, the table is resized to double its current number of buckets. During resizing:

1. A new bucket array (twice the size) is allocated with `calloc`.
2. Every existing entry is rehashed: its new index is `hash_key(entry->key) % new_size`.
3. Each entry is moved (not copied) into the appropriate bucket of the new array.
4. The old bucket array is freed.

Rehashing is necessary because the bucket index depends on the table size. When the table grows from 8 to 16 buckets, a key that previously hashed to bucket 3 might now belong in bucket 11. Without rehashing, lookups would search the wrong bucket and fail to find existing keys.

## Commands

| Command | Syntax | Description | Example |
|---------|--------|-------------|---------|
| SET | `SET key value` | Store a key-value pair | `SET name Atishya` |
| SET with TTL | `SET key value EX seconds` | Store with expiration | `SET session abc123 EX 300` |
| GET | `GET key` | Retrieve a value | `GET name` |
| DEL | `DEL key` | Delete a key | `DEL name` |
| TTL | `TTL key` | Get remaining time to live | `TTL session` |
| PING | `PING` | Check if the server is alive | `PING` |
| EXIT | `EXIT` | Rewrite AOF and close connection | `EXIT` |

**SET** responds with `OK`. If `EX` is provided, the key will expire after the given number of seconds. A plain `SET` on a key that already has a TTL will remove the expiration and make the key persistent. Values with spaces can be quoted: `SET message "Hello World"`.

**GET** responds with the stored value, or `(nil)` if the key does not exist or has expired.

**DEL** responds with `1` if the key was deleted, or `0` if the key did not exist (or was expired).

**TTL** responds with the number of seconds remaining, `-1` if the key exists but has no expiration, or `-2` if the key does not exist or has expired.

**PING** responds with `PONG`.

**EXIT** triggers an AOF rewrite (compaction of the persistence file), responds with `BYE`, and then closes the client connection. The server itself continues running for other clients.

Command names are case-insensitive (`set`, `Set`, `SET` all work). The `EX` keyword is also case-insensitive. Unrecognized commands return `ERR unknown command`.

## TTL and Expiration

When a key is set with `SET key value EX 60`, the server computes an absolute expiration timestamp: `expires_at = time(NULL) + 60`. This absolute timestamp (not the relative duration) is stored both in the in-memory `Entry` struct and in the AOF file.

**Lazy expiration:** There is no background thread that periodically scans for expired keys. Instead, expiration is checked at access time:

- `GET` on an expired key returns `(nil)` but does not physically remove the entry from the hash table. This is intentional: `GET` runs under a read lock, and deleting an entry would require a write lock.
- `TTL` on an expired key returns `-2` but also does not remove the entry.
- `DEL` on an expired key returns `0` (treated as non-existent).

Expired entries remain in memory until they are overwritten by a new `SET`, explicitly deleted, or cleaned up during an AOF rewrite/reload.

**Expiration and persistence:** The AOF stores expiration as `EXAT <unix_timestamp>`. When the server restarts and loads the AOF, any key whose timestamp has already passed is skipped and not loaded into memory. This means keys that expired while the server was offline are automatically discarded during recovery.

**Overwriting clears TTL:** If a key has a TTL and you run `SET key newvalue` without `EX`, the expiration is removed. The key becomes persistent (no longer expires). This is handled by setting `expires_at = 0` on the entry after the SET.

## Persistence

InMemDB uses an append-only file (`data.aof`) for persistence. Every mutating command (SET and DEL) is appended to this file before the in-memory hash table is modified. If the AOF write fails, the command is rejected and the in-memory state is not changed.

**AOF format:** Keys and values are length-prefixed to handle spaces and special characters safely.

```
SET 4:name7:Atishya
SET 7:session6:abc123 EXAT 1750000300
DEL 4:name
```

Each line is one operation. For SET, the format is `SET <key_length>:<key><value_length>:<value>`. If the key has an expiration, ` EXAT <unix_timestamp>` is appended. For DEL, the format is `DEL <key_length>:<key>`.

**Startup recovery:** When the server starts, `persistence_load()` reads `data.aof` line by line and replays the operations into the hash table. SET commands call `hash_table_set_with_expiry()`. DEL commands call `hash_table_delete()`. Keys that have already expired (based on their EXAT timestamp) are skipped during loading. If the file does not exist, the server starts with an empty database.

**AOF rewrite:** Over time, the AOF can grow large with redundant operations (for example, the same key being set many times). The EXIT command triggers `persistence_rewrite()`, which:

1. Opens a temporary file `data.aof.tmp` for writing.
2. Iterates over every entry in the hash table.
3. Writes only non-expired entries as SET lines (with EXAT if applicable).
4. Closes the temporary file.
5. Calls `rename("data.aof.tmp", "data.aof")` to atomically replace the old AOF.

This compacts the file to contain only the current state of the database.

**File handling:** The AOF is opened and closed for every individual append operation. There is no persistent file handle kept open between commands.

## Networking

The server creates a TCP socket using `socket(AF_INET, SOCK_STREAM, 0)` and sets the `SO_REUSEADDR` option so the port can be reused immediately after a restart. It binds to `127.0.0.1:6379` and calls `listen()` with a backlog of 5.

The main server loop uses `select()` to monitor three categories of file descriptors:

1. **The server socket**, to detect incoming connections.
2. **The notification pipe** (read end), to receive client-close signals from worker threads.
3. **All active client sockets**, to detect incoming data.

When a new client connects, `accept()` returns a new file descriptor which is stored in one of 16 available client slots. If all 16 slots are occupied, the server sends `ERR server busy` and closes the connection.

Each client has its own receive buffer (1024 bytes). Data from `recv()` is appended to the buffer, and the server scans for newline characters to extract complete commands. Both `\n` and `\r\n` line endings are handled. If a command exceeds the buffer size, the server responds with `ERR command too long` and resets that client's buffer.

When a client disconnects (recv returns 0) or encounters a read error, the server calls `shutdown()` and `close()` on the file descriptor and frees the client slot.

Workers send responses directly to clients using `send()` with the `MSG_NOSIGNAL` flag, which prevents a broken pipe from crashing the server with SIGPIPE.

## Concurrency

InMemDB uses a producer-consumer model with a shared task queue and a pool of 4 worker threads.

**Task queue:** The queue is a singly linked list of `Task` structs, each containing a client file descriptor and a command string (up to 1024 bytes). The server thread (producer) appends tasks to the tail. Worker threads (consumers) pop tasks from the head.

**Three synchronization primitives are used:**

1. **Queue mutex (`pthread_mutex_t queue_mutex`):** Protects all access to the task queue. Both the server thread (when submitting tasks) and worker threads (when popping tasks) lock this mutex before touching the queue.

2. **Condition variable (`pthread_cond_t queue_cond`):** Workers sleep on this condition variable when the queue is empty. When the server submits a new task, it calls `pthread_cond_signal()` to wake one sleeping worker. During shutdown, `pthread_cond_broadcast()` wakes all workers so they can exit.

3. **Database read-write lock (`pthread_rwlock_t db_lock`):** This is the lock that protects the hash table. Before executing a command, each worker determines if the command is a read or write:
   - **Read commands (GET, TTL):** The worker acquires a read lock with `pthread_rwlock_rdlock()`. Multiple GET and TTL commands from different workers can execute at the same time because read locks are shared.
   - **Write commands (SET, DEL, PING, EXIT, unknown):** The worker acquires a write lock with `pthread_rwlock_wrlock()`. This is exclusive: no other read or write can proceed until the write lock is released.

This means that under a workload of mostly reads, multiple workers can serve GET and TTL requests concurrently. Write commands serialize access to prevent data races.

**Notification pipe:** When a worker processes an EXIT command, it needs to tell the server thread to close that client's connection. The worker cannot close the socket directly because the server's `select()` loop manages the client file descriptor array. Instead, the worker writes the client's file descriptor (as an `int`) to the write end of a pipe. The server monitors the read end of this pipe in its `select()` call, reads the file descriptor, and closes the connection.

**Graceful shutdown:** `worker_pool_destroy()` sets a `shutting_down` flag, broadcasts the condition variable to wake all workers, and then calls `pthread_join()` on each of the 4 threads. After all threads have exited, any remaining tasks in the queue are freed, and all synchronization primitives (mutex, condition variable, read-write lock) are destroyed. The pipe file descriptors are closed.

## Project Structure

```
InMemDB/
├── include/
│   ├── command.h          Command parsing and execution interface
│   ├── entry.h            Entry struct definition and expiry functions
│   ├── hash_table.h       HashTable struct, constants, hash table operations
│   ├── persistence.h      AOF append, load, and rewrite interface
│   ├── server.h           TCP server interface (server_start)
│   └── worker_pool.h      Task and WorkerPool structs, pool operations
├── src/
│   ├── main.c             Entry point: creates table, loads AOF, starts server
│   ├── entry.c            Entry creation, destruction, expiry logic
│   ├── hash_table.c       Hash table: set, get, delete, resize, hash function
│   ├── command.c           Command parser and executor for all 6 commands
│   ├── persistence.c      AOF file writing, reading, and rewrite
│   ├── server.c           TCP server with select() loop and client management
│   └── worker_pool.c      Thread pool, task queue, locking, notification pipe
├── Makefile               Build configuration
├── LICENSE                MIT License
└── README.md              This file
```

## Building the Project

```bash
make
```

This compiles all source files using GCC with the following flags:

- `-Wall -Wextra`: enables extensive compiler warnings
- `-std=c17`: uses the C17 standard
- `-D_XOPEN_SOURCE=700`: enables POSIX functions
- `-pthread`: links the POSIX threads library
- `-Iinclude`: adds the include directory to the header search path

The output binary is `inmem`.

## Running the Server

```bash
make run
```

This builds the project (if needed) and runs `./inmem`. The server starts listening on `127.0.0.1:6379`.

## Cleaning

```bash
make clean
```

This removes the compiled binary (`inmem`).

## Manual Usage

Once the server is running, you can connect using netcat:

```bash
nc 127.0.0.1 6379
```

Then type commands followed by Enter:

```
SET name Atishya
OK
GET name
Atishya
SET session token123 EX 60
OK
TTL session
60
PING
PONG
DEL name
1
GET name
(nil)
EXIT
BYE
```

Multiple clients can connect simultaneously by opening netcat in separate terminals.

## Design Decisions

**Hash table with separate chaining:** Collisions are resolved using linked lists rather than open addressing (like linear probing). Chaining is simpler to implement, does not require a deletion marker strategy, and allows the table to hold more entries than the number of buckets without degrading.

**Lazy expiration instead of active expiration:** Expired keys are not removed by a background thread. They are simply treated as non-existent when accessed. This avoids the complexity of a dedicated expiration thread and sidesteps the locking issue where GET (which holds a read lock) would need to delete entries (which requires a write lock). The tradeoff is that expired entries stay in memory until they are overwritten or cleaned up during a rewrite.

**Persistence-first writes:** The AOF is written before the in-memory hash table is modified. If the disk write fails, the command is rejected and memory is not changed. This ensures that any data visible to clients is also on disk. The alternative (writing memory first) would risk data loss if the process crashed between the memory write and the disk write.

**AOF with length-prefixed format:** Keys and values are prefixed with their lengths (e.g., `4:name7:Atishya`). This allows values containing spaces, colons, and other special characters to be stored and recovered correctly. A simpler whitespace-delimited format would break for values like "Hello World".

**Worker pool instead of thread-per-client:** The server uses a fixed pool of 4 worker threads rather than spawning a new thread for each client. This limits resource usage and avoids the overhead of frequent thread creation and destruction. The task queue decouples the rate of incoming commands from the number of threads.

**Read-write lock instead of a plain mutex:** A read-write lock allows multiple GET and TTL operations to execute concurrently, since they do not modify the hash table. A plain mutex would force all operations to execute one at a time, even read-only ones. The read-write lock improves throughput for read-heavy workloads.

**Notification pipe for cross-thread communication:** When a worker needs to close a client connection (after EXIT), it cannot modify the server's client array directly. The pipe provides a safe, `select()`-compatible way for workers to signal the server thread. This avoids sharing the client file descriptor array between threads and eliminates the need for additional locking around the client management code.

## Limitations

- No authentication. Any client that can reach port 6379 can execute commands.
- No replication or clustering. This is a single-server, single-process system.
- No LRU eviction or memory limits. The database will grow until the system runs out of memory.
- No RESP protocol. The server uses a plain text, newline-delimited format. Standard Redis clients (like `redis-cli`) will not work.
- No signal handling. There is no SIGINT or SIGTERM handler, so killing the server process with Ctrl+C does not trigger an AOF rewrite. Data since the last EXIT command may be recoverable from the existing AOF, but any partial writes at the end of the file could cause issues.
- The hash table only grows, never shrinks. Deleting many keys does not reduce the bucket array size.
- The server listens only on `127.0.0.1` (localhost). Remote connections are not accepted.
- Expired entries are not physically deleted by read operations. They accumulate in memory until overwritten, explicitly deleted, or cleaned during rewrite/reload.

## What This Project Demonstrates

- **Hash tables:** bucket arrays, hash functions, separate chaining, load factor management, dynamic resizing, and rehashing.
- **Manual memory management in C:** `malloc`, `calloc`, `free`, `strcpy`, ownership of heap-allocated strings, cleanup on error paths, and structured destruction functions.
- **Append-only file persistence:** writing operations to disk before modifying memory, replaying a log to reconstruct state, compacting the log, and handling expiration across restarts.
- **TCP socket programming:** `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()`, `SO_REUSEADDR`, `MSG_NOSIGNAL`, and `select()` for I/O multiplexing.
- **Multithreading with POSIX threads:** `pthread_create`, `pthread_join`, `pthread_mutex`, `pthread_cond_wait`, `pthread_cond_signal`, `pthread_cond_broadcast`, and `pthread_rwlock`.
- **Producer-consumer pattern:** a shared task queue where the server thread produces tasks and worker threads consume them.
- **Read-write locking:** allowing concurrent reads while serializing writes, and understanding why certain operations need which type of lock.
- **Inter-thread communication:** using a pipe to pass data from worker threads back to the server's `select()` loop.
- **Command parsing:** tokenizing input strings, handling quoted arguments, and case-insensitive matching.
- **Lazy expiration:** checking timestamps at access time rather than using a background cleanup thread, and understanding the tradeoffs of this approach.

## Future Improvements

These are features the project does not currently have, but could be added:

- **Signal handling:** Add a SIGINT/SIGTERM handler that triggers an AOF rewrite and graceful shutdown when the server is killed.
- **Configurable bind address and port:** Allow the server to accept a custom IP address and port instead of hardcoded `127.0.0.1:6379`.
- **Background expiration:** A periodic cleanup thread that scans for and removes expired entries to reclaim memory.
- **Additional commands:** KEYS, EXISTS, RENAME, PERSIST, or multi-key DEL.
- **RESP protocol support:** Implementing the Redis serialization protocol so standard Redis clients can connect.
- **Configurable worker count and table size:** Accept these as command-line arguments or from a config file.
- **`fsync` for durability:** The current AOF writes use `fclose()` but do not call `fsync()`. Adding `fsync()` would ensure data reaches disk even if the OS crashes.

## License

MIT License. See [LICENSE](LICENSE) for details.