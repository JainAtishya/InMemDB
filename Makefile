CC = gcc

CFLAGS = -Wall -Wextra -std=c17 -D_XOPEN_SOURCE=700 -pthread -Iinclude

SRCS = src/main.c src/entry.c src/hash_table.c src/command.c \
       src/persistence.c src/server.c src/worker_pool.c

TARGET = inmem

# Build the server
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

# Run the server
run: $(TARGET)
	./$(TARGET)

# Remove the compiled binary
clean:
	rm -f $(TARGET)

.PHONY: all run clean