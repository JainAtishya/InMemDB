#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "server.h"
#include "command.h"

#define SERVER_IP "127.0.0.1"
#define BACKLOG 5
#define BUFFER_SIZE 1024


int server_start(int port, HashTable *table)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_addr_len =
        sizeof(client_addr);

    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];


    /* ================= CREATE SOCKET ================= */

    server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (server_fd == -1)
    {
        perror("socket");
        return 0;
    }

    printf(
        "Socket created. FD = %d\n",
        server_fd);


    /* ================= SERVER ADDRESS ================= */

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

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


    /* ================= BIND ================= */

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
        port);


    /* ================= LISTEN ================= */

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
        port);


    /* ================= ACCEPT CLIENT ================= */

    client_fd = accept(
        server_fd,
        (struct sockaddr *)&client_addr,
        &client_addr_len);

    if (client_fd == -1)
    {
        perror("accept");

        close(server_fd);

        return 0;
    }

    printf(
        "Client connected. Client FD = %d\n",
        client_fd);


    /* ================= CLIENT LOOP ================= */

    while (1)
    {
        memset(
            buffer,
            0,
            sizeof(buffer));

        ssize_t bytes_received =
            recv(
                client_fd,
                buffer,
                sizeof(buffer) - 1,
                0);

        if (bytes_received == -1)
        {
            perror("recv");

            break;
        }

        if (bytes_received == 0)
        {
            printf("Client disconnected.\n");

            break;
        }

        buffer[bytes_received] = '\0';

        printf(
            "Received: %s",
            buffer);


        /* Remove trailing newline */
        buffer[strcspn(buffer, "\r\n")] = '\0';


        /* ================= EXECUTE COMMAND ================= */

        command_execute(
            table,
            buffer,
            response,
            sizeof(response));


        /* ================= SEND RESPONSE ================= */

        if (send(
                client_fd,
                response,
                strlen(response),
                0) == -1)
        {
            perror("send");

            break;
        }


        /*
         * EXIT closes this client connection.
         * For now the server itself also stops.
         */
        if (strcmp(buffer, "EXIT") == 0 ||
            strcmp(buffer, "exit") == 0)
        {
            break;
        }
    }


    /* ================= CLEANUP ================= */

    close(client_fd);
    close(server_fd);

    return 1;
}