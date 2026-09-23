#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "server.h"
#include "command.h"

#define SERVER_IP "127.0.0.1"
#define BACKLOG 5

#define MAX_CLIENTS 16
#define BUFFER_SIZE 1024


/*
 * Send the complete response.
 *
 * send() is allowed to send fewer bytes than requested,
 * so we keep sending until the entire response is transmitted.
 */
static int send_all(int fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            fd,
            data + total_sent,
            length - total_sent,
            0
        );

        if (sent == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("send");
            return 0;
        }

        total_sent += (size_t)sent;
    }

    return 1;
}


/*
 * Close a client and remove it from the client list.
 */
static void close_client(
    int client_fd,
    int client_fds[],
    char client_buffers[][BUFFER_SIZE],
    size_t client_buffer_lengths[],
    int max_clients
)
{
    for (int i = 0; i < max_clients; i++)
    {
        if (client_fds[i] == client_fd)
        {
            close(client_fds[i]);

            client_fds[i] = -1;
            client_buffers[i][0] = '\0';
            client_buffer_lengths[i] = 0;

            printf("Client FD %d disconnected.\n", client_fd);

            return;
        }
    }
}


int server_start(int port, HashTable *table)
{
    int server_fd;

    struct sockaddr_in server_addr;


    /*
     * ============================================================
     * CREATE SERVER SOCKET
     * ============================================================
     */

    server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_fd == -1)
    {
        perror("socket");
        return 0;
    }

    printf(
        "Socket created. FD = %d\n",
        server_fd
    );


    /*
     * ============================================================
     * REUSE ADDRESS
     * ============================================================
     *
     * Allows the server to restart quickly after being stopped.
     */

    int reuse = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)) == -1)
    {
        perror("setsockopt");

        close(server_fd);

        return 0;
    }


    /*
     * ============================================================
     * SERVER ADDRESS
     * ============================================================
     */

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family = AF_INET;

    server_addr.sin_port = htons(port);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");

        close(server_fd);

        return 0;
    }


    /*
     * ============================================================
     * BIND
     * ============================================================
     */

    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)) == -1)
    {
        perror("bind");

        close(server_fd);

        return 0;
    }

    printf(
        "Socket bound to %s:%d\n",
        SERVER_IP,
        port
    );


    /*
     * ============================================================
     * LISTEN
     * ============================================================
     */

    if (listen(
            server_fd,
            BACKLOG) == -1)
    {
        perror("listen");

        close(server_fd);

        return 0;
    }

    printf(
        "Server listening on %s:%d\n",
        SERVER_IP,
        port
    );


    /*
     * ============================================================
     * CLIENT TABLE
     * ============================================================
     *
     * Each connected client gets its own socket FD.
     *
     * -1 means the slot is unused.
     */

    int client_fds[MAX_CLIENTS];

    char client_buffers[MAX_CLIENTS][BUFFER_SIZE];

    size_t client_buffer_lengths[MAX_CLIENTS];


    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        client_fds[i] = -1;

        client_buffers[i][0] = '\0';

        client_buffer_lengths[i] = 0;
    }


    /*
     * ============================================================
     * EVENT LOOP
     * ============================================================
     *
     * The server remains single-threaded.
     *
     * select() tells us which sockets are ready for I/O.
     */

    while (1)
    {
        fd_set read_fds;

        FD_ZERO(&read_fds);

        /*
         * Always monitor the listening socket.
         *
         * If it becomes readable, a new client is waiting.
         */

        FD_SET(server_fd, &read_fds);

        int max_fd = server_fd;


        /*
         * Add all connected clients to the monitored set.
         */

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (client_fds[i] != -1)
            {
                FD_SET(
                    client_fds[i],
                    &read_fds
                );

                if (client_fds[i] > max_fd)
                {
                    max_fd = client_fds[i];
                }
            }
        }


        /*
         * ========================================================
         * WAIT
         * ========================================================
         *
         * select() blocks until at least one socket is ready.
         *
         * This is NOT a busy loop.
         */

        int ready = select(
            max_fd + 1,
            &read_fds,
            NULL,
            NULL,
            NULL
        );

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");

            break;
        }


        /*
         * ========================================================
         * NEW CLIENT
         * ========================================================
         */

        if (FD_ISSET(server_fd, &read_fds))
        {
            struct sockaddr_in client_addr;

            socklen_t client_addr_len =
                sizeof(client_addr);

            int client_fd = accept(
                server_fd,
                (struct sockaddr *)&client_addr,
                &client_addr_len
            );

            if (client_fd == -1)
            {
                perror("accept");
            }
            else
            {
                int added = 0;

                for (int i = 0; i < MAX_CLIENTS; i++)
                {
                    if (client_fds[i] == -1)
                    {
                        client_fds[i] = client_fd;

                        client_buffers[i][0] = '\0';

                        client_buffer_lengths[i] = 0;

                        printf(
                            "Client connected. Client FD = %d\n",
                            client_fd
                        );

                        added = 1;

                        break;
                    }
                }

                /*
                 * No free client slot.
                 */

                if (!added)
                {
                    printf(
                        "Maximum clients reached. Rejecting FD %d.\n",
                        client_fd
                    );

                    close(client_fd);
                }
            }
        }


        /*
         * ========================================================
         * HANDLE EXISTING CLIENTS
         * ========================================================
         */

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int client_fd = client_fds[i];

            if (client_fd == -1)
            {
                continue;
            }


            /*
             * Was this client socket ready?
             */

            if (!FD_ISSET(client_fd, &read_fds))
            {
                continue;
            }


            /*
             * Temporary receive buffer.
             */

            char temp[BUFFER_SIZE];

            ssize_t bytes_received = recv(
                client_fd,
                temp,
                sizeof(temp) - 1,
                0
            );


            /*
             * ====================================================
             * CLIENT DISCONNECTED
             * ====================================================
             */

            if (bytes_received == 0)
            {
                close_client(
                    client_fd,
                    client_fds,
                    client_buffers,
                    client_buffer_lengths,
                    MAX_CLIENTS
                );

                continue;
            }


            /*
             * ====================================================
             * RECEIVE ERROR
             * ====================================================
             */

            if (bytes_received == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("recv");

                close_client(
                    client_fd,
                    client_fds,
                    client_buffers,
                    client_buffer_lengths,
                    MAX_CLIENTS
                );

                continue;
            }


            /*
             * ====================================================
             * APPEND RECEIVED DATA TO CLIENT BUFFER
             * ====================================================
             *
             * Important:
             *
             * TCP is a byte stream.
             *
             * One recv() does NOT necessarily equal one command.
             *
             * Therefore we keep a buffer for each client.
             */

            size_t received =
                (size_t)bytes_received;

            if (client_buffer_lengths[i] + received
                >= BUFFER_SIZE)
            {
                const char *error =
                    "ERR command too long\n";

                send_all(
                    client_fd,
                    error,
                    strlen(error)
                );

                client_buffer_lengths[i] = 0;

                client_buffers[i][0] = '\0';

                continue;
            }


            memcpy(
                client_buffers[i] +
                    client_buffer_lengths[i],

                temp,

                received
            );

            client_buffer_lengths[i] += received;

            client_buffers[i][
                client_buffer_lengths[i]
            ] = '\0';


            /*
             * ====================================================
             * PROCESS COMPLETE LINES
             * ====================================================
             *
             * Our protocol is currently line based:
             *
             * SET name Atishya\n
             * GET name\n
             * DEL name\n
             *
             * A client may send multiple commands in one recv(),
             * so we process every complete line.
             */

            while (1)
            {
                char *newline =
                    strchr(
                        client_buffers[i],
                        '\n'
                    );

                if (newline == NULL)
                {
                    break;
                }


                /*
                 * Determine command length.
                 */

                size_t command_length =
                    (size_t)(
                        newline -
                        client_buffers[i]
                    );


                /*
                 * Remove CR from CRLF.
                 */

                if (command_length > 0 &&
                    client_buffers[i][
                        command_length - 1
                    ] == '\r')
                {
                    command_length--;
                }


                /*
                 * Copy command into a separate buffer.
                 */

                char command[BUFFER_SIZE];

                memcpy(
                    command,
                    client_buffers[i],
                    command_length
                );

                command[command_length] = '\0';


                /*
                 * =================================================
                 * EXECUTE COMMAND
                 * =================================================
                 */

                char response[BUFFER_SIZE];

                int result =
                    command_execute(
                        table,
                        command,
                        response,
                        sizeof(response)
                    );


                /*
                 * =================================================
                 * SEND RESPONSE
                 * =================================================
                 */

                if (response[0] != '\0')
                {
                    if (!send_all(
                            client_fd,
                            response,
                            strlen(response)))
                    {
                        close_client(
                            client_fd,
                            client_fds,
                            client_buffers,
                            client_buffer_lengths,
                            MAX_CLIENTS
                        );

                        break;
                    }
                }


                /*
                 * =================================================
                 * EXIT
                 * =================================================
                 *
                 * EXIT closes only this client.
                 *
                 * The server keeps running.
                 */

                if (result == 1)
                {
                    close_client(
                        client_fd,
                        client_fds,
                        client_buffers,
                        client_buffer_lengths,
                        MAX_CLIENTS
                    );

                    break;
                }


                /*
                 * =================================================
                 * REMOVE PROCESSED COMMAND
                 * =================================================
                 */

                size_t remaining =
                    client_buffer_lengths[i]
                    - (size_t)(
                        (newline -
                         client_buffers[i]) + 1
                    );

                memmove(
                    client_buffers[i],
                    newline + 1,
                    remaining
                );

                client_buffer_lengths[i] =
                    remaining;

                client_buffers[i][remaining] =
                    '\0';
            }
        }
    }


    /*
     * ============================================================
     * CLEANUP
     * ============================================================
     */

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (client_fds[i] != -1)
        {
            close(client_fds[i]);
        }
    }

    close(server_fd);

    return 1;
}