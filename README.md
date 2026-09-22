# InMemDB

A lightweight, Redis-inspired in-memory key-value database written from scratch in C.

InMemDB is a systems-oriented learning project focused on understanding how an in-memory database works internally. Instead of relying on existing database libraries, core components are implemented manually to explore hash tables, memory management, persistence, file I/O, networking, sockets, and concurrency.

The project is intentionally built incrementally, with each layer introducing a new systems concept.

---

## Project Goals

The primary goal is not to build a production-ready database.

The goal is to understand what happens underneath a database abstraction.

By building the system from scratch, the project explores:

- Hash table implementation
- Hash functions and collision handling
- Separate chaining
- Dynamic resizing and rehashing
- Memory allocation and deallocation
- Command parsing
- File I/O
- Append-Only File (AOF) persistence
- Persistence recovery
- AOF rewriting / compaction
- TCP socket programming
- Client-server architecture
- File descriptors
- Concurrent client handling
- Multithreading
- Basic database-server architecture

---

# What Are We Building?

At its core, InMemDB is a key-value store.

A user can interact with it using commands such as:

```text
SET name Atishya
GET name
DEL name
```

Example:

```text
InMemDB> SET name Atishya
OK

InMemDB> GET name
Atishya

InMemDB> SET message "Hello World from InMemDB"
OK

InMemDB> GET message
Hello World from InMemDB

InMemDB> DEL name
1

InMemDB> GET name
(nil)
```

The database keeps active key-value pairs in memory for fast access.

---

# Architecture

InMemDB is being developed in multiple layers.

```text
                    InMemDB
                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼
   Data Layer      Command Layer   Persistence
        │              │              │
        ▼              ▼              ▼
   Hash Table      CLI Parser       AOF
        │                             │
        ▼                             ▼
   Memory Mgmt                    Recovery
                                      │
                                      ▼
                               AOF Rewriting
                                      │
                                      ▼
                                Networking
                                      │
                                      ▼
                              Client / Server
                                      │
                                      ▼
                                Concurrency
```

Each layer is implemented and tested before moving to the next.

---

# 1. In-Memory Data Structure

The foundation of InMemDB is a custom hash table implemented in C.

The hash table stores:

```text
key → value
```

For example:

```text
name → Atishya
age  → 21
city → Delhi
```

The implementation uses:

- A dynamically allocated bucket array
- A custom hash function
- Separate chaining for collisions
- Linked-list entries
- Dynamic memory allocation
- Key/value copying
- Entry deletion

## Collision Handling

Multiple keys can map to the same bucket.

InMemDB uses separate chaining:

```text
Bucket 3
   │
   ▼
┌──────────────┐
│ key1 → value │
│ next ────────┼──────┐
└──────────────┘      │
                      ▼
                ┌──────────────┐
                │ key2 → value │
                │ next = NULL  │
                └──────────────┘
```

---

# 2. Dynamic Resizing and Rehashing

The hash table does not remain fixed in size.

As more keys are inserted, the load factor increases.

```text
load factor = number of entries / number of buckets
```

When the load factor crosses the configured threshold, the table is resized.

The bucket index is calculated using:

```text
index = hash(key) % table_size
```

When `table_size` changes, the resulting index can also change. Therefore,
existing entries must be rehashed.

```text
Old bucket array
       │
       ▼
Allocate larger bucket array
       │
       ▼
Recalculate bucket index
for every existing entry
       │
       ▼
Move entries
       │
       ▼
Free old bucket array
```

---

# 3. Command Layer

InMemDB provides a command-line interface.

Supported commands currently include:

```text
SET key value
GET key
DEL key
EXIT
```

The parser supports quoted values:

```text
SET message "Hello World from InMemDB"
```

which becomes:

```text
command = SET
key     = message
value   = Hello World from InMemDB
```

The command layer is separated from the hash-table layer:

```text
User Input
    │
    ▼
Command Parser
    │
    ▼
Command Handler
    │
    ▼
Hash Table
```

---

# 4. Persistence

An in-memory database loses its contents when the process terminates.

InMemDB implements persistence using an **Append-Only File (AOF)**.

Every modifying operation is recorded in:

```text
data.aof
```

Example:

```text
SET 4:name7:Atishya
SET 3:age2:21
DEL 3:age
```

Length prefixes allow values containing spaces to be represented safely.

For example:

```text
SET message "Hello World"
```

can be persisted without relying on whitespace-based parsing.

---

# 5. AOF Recovery

When InMemDB starts, it reads the AOF and reconstructs the in-memory state.

```text
Start InMemDB
      │
      ▼
Create HashTable
      │
      ▼
Read data.aof
      │
      ▼
Replay operations
      │
      ▼
Reconstruct HashTable
      │
      ▼
Start accepting commands
```

This demonstrates the basic idea behind log-based persistence and recovery.

---

# 6. AOF Rewriting

If every operation is appended forever, the AOF can grow unnecessarily large.

For example:

```text
SET name Atishya
SET name Rahul
SET name Alex
DEL name
SET name John
```

Only the final state matters:

```text
name → John
```

Therefore, InMemDB supports AOF rewriting.

The current in-memory state is written into a temporary file and then used to
replace the old AOF.

```text
Current HashTable
       │
       ▼
Create temporary AOF
       │
       ▼
Write current state
       │
       ▼
Close temporary file
       │
       ▼
Replace old AOF
```

This keeps the persistence file compact while preserving the current state.

---

# 7. Networking

The next major layer of InMemDB is networking.

Currently, the database is accessed through its command-line interface.

The goal is to turn InMemDB into a proper client-server application.

```text
             TCP
Client ──────────────────► InMemDB Server
                              │
                              ▼
                           HashTable
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
                 Memory              AOF
```

The networking layer will introduce:

- TCP
- IP addresses
- Ports
- Sockets
- File descriptors
- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `recv()`
- `send()`
- `close()`

The initial implementation will intentionally use a simple single-threaded
server architecture.

---

# 8. File Descriptors and Sockets

One goal of the networking layer is to understand the relationship between
Unix file descriptors and sockets.

A socket is represented inside the process using a file descriptor.

The basic server flow is:

```text
socket()
   │
   ▼
bind()
   │
   ▼
listen()
   │
   ▼
accept()
   │
   ▼
recv() / send()
   │
   ▼
close()
```

The listening socket and an accepted client connection are represented by
different file descriptors.

Conceptually:

```text
server_fd
    │
    ▼
Listening Socket
    │
    │ accept()
    ▼
client_fd
    │
    ▼
Client Connection
    │
    ├── recv()
    └── send()
```

---

# 9. Concurrency

After the basic networking implementation works, the project can be extended
to handle multiple clients.

One possible approach is a thread-per-client architecture.

```text
                Server
                  │
               accept()
                  │
        ┌─────────┼─────────┐
        ▼         ▼         ▼
     Thread 1  Thread 2  Thread 3
        │         │         │
     Client 1  Client 2  Client 3
```

This introduces:

- Threads
- Shared memory
- Race conditions
- Synchronization
- Mutexes
- Thread safety

A later extension could explore event-driven approaches such as `poll()` or
`epoll()`.

---

# Current Command Set

| Command | Description |
|---------|-------------|
| `SET key value` | Insert or update a key |
| `GET key` | Retrieve a value |
| `DEL key` | Delete a key |
| `EXIT` | Exit the database |

Quoted values are supported:

```text
SET message "Hello World from InMemDB"
```

---

# Project Structure

```text
InMemDB/
│
├── include/
│   ├── entry.h
│   ├── hash_table.h
│   ├── command.h
│   └── persistence.h
│
├── src/
│   ├── main.c
│   ├── entry.c
│   ├── hash_table.c
│   ├── command.c
│   └── persistence.c
│
├── data.aof
├── Makefile
├── README.md
└── .gitignore
```

The networking layer will introduce additional source/header files as the
project evolves.

---

# Design Principles

### 1. Build from fundamentals

Important components are implemented manually instead of relying on existing
database or networking abstractions.

### 2. Understand before abstracting

Each layer is understood independently before being integrated into the larger
system.

### 3. Keep responsibilities separated

```text
HashTable      → data storage
Command        → user interaction
Persistence    → durability
Networking     → client communication
```

### 4. Prefer explicit memory management

Memory allocation and deallocation are handled manually using C's memory
management facilities.

### 5. Learn through incremental implementation

The project is developed in stages rather than implementing the entire
database at once.

---

# Learning Roadmap

```text
                    InMemDB
                       │
                       ▼
              Hash Table Foundation
                       │
                       ▼
             Collision Handling
                       │
                       ▼
             Resizing + Rehashing
                       │
                       ▼
                 CLI Commands
                       │
                       ▼
                  Persistence
                       │
                       ▼
                AOF Recovery
                       │
                       ▼
               AOF Rewriting
                       │
                       ▼
                  Networking
                       │
                       ▼
                TCP Server
                       │
                       ▼
              Multiple Clients
                       │
                       ▼
                 Concurrency
```

---

# What This Project Is Teaching

The project is intentionally broader than implementing a HashMap.

It connects multiple low-level concepts into one working system:

```text
Hash Table
    ↓
How data is stored in memory

Memory Management
    ↓
How data is allocated and released

Persistence
    ↓
How data survives process termination

AOF
    ↓
How operations can be logged and replayed

Networking
    ↓
How external clients communicate with the database

Sockets
    ↓
How the OS exposes network communication

Concurrency
    ↓
How multiple clients can interact with the server
```

The final result is a small demonstration of how different systems concepts
come together to form a database server.

---

# Status

## Completed

- [x] Custom hash table
- [x] Hash function
- [x] Separate chaining
- [x] Dynamic resizing
- [x] Rehashing
- [x] SET
- [x] GET
- [x] DEL
- [x] Hash table update functionality
- [x] CLI command parser
- [x] Quoted values
- [x] AOF persistence
- [x] AOF recovery
- [x] AOF rewriting / compaction

## In Progress

- [ ] TCP networking layer
- [ ] Client-server communication
- [ ] Remote command execution

## Planned

- [ ] Multiple client connections
- [ ] Concurrent client handling
- [ ] Thread-based server
- [ ] Synchronization / thread safety
- [ ] Additional Redis-inspired commands
- [ ] Further performance and reliability improvements

---

# Building

The project currently uses GCC and the C17 standard.

```bash
gcc -Wall -Wextra -std=c17 -Iinclude src/main.c src/entry.c src/hash_table.c src/command.c src/persistence.c -o inmem
```

Run:

```bash
./inmem
```

---

# Example

```text
InMemDB started.
Type EXIT to quit.

InMemDB> SET name Atishya
OK

InMemDB> SET message "Hello World from InMemDB"
OK

InMemDB> GET name
Atishya

InMemDB> GET message
Hello World from InMemDB

InMemDB> DEL name
1

InMemDB> GET name
(nil)
```

After restarting the database, persisted data can be reconstructed from the
AOF.

---

# Why InMemDB?

InMemDB is primarily a learning project.

The intention is to understand implementation details that are normally hidden
behind a database API.

Rather than simply using:

```text
SET
GET
DEL
```

the project asks:

- How is the data actually stored?
- How are hash collisions handled?
- When should the table resize?
- What happens to entries during rehashing?
- How can memory be allocated and released safely?
- How can data survive a process crash or restart?
- How does a server communicate with clients?
- What exactly is a socket?
- Why does a socket have a file descriptor?
- How does `accept()` create a client connection?
- How can one server handle multiple clients?
- What changes when concurrency is introduced?

These questions drive the implementation.

---

# Disclaimer

InMemDB is an educational project inspired by concepts found in systems such
as Redis and other key-value databases.

It is **not intended to be production-ready** and does not attempt to replicate
Redis internally or provide its full feature set.

The focus is on learning systems programming, data structures, operating-system
interfaces, networking, persistence, and database-server architecture through
implementation.
