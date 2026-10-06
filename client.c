/* client.c - TCP Chat Client */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define PORT 5000
#define BUFFER_SIZE 1024

int main()
{
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    /* Create socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    /* Configure server address */
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &server_addr.sin_addr) <= 0)
    {
        perror("Invalid address");
        close(sock);
        return 1;
    }

    /* Connect to server */
    if (connect(sock,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        return 1;
    }

    printf("Connected to chat server!\n");

    /*
     * fork() creates two processes.
     *
     * Child  -> receives messages
     * Parent -> sends messages
     */

    pid_t pid = fork();

    if (pid < 0)
    {
        perror("Fork failed");
        close(sock);
        return 1;
    }

    /* Child process */
    if (pid == 0)
    {
        while (1)
        {
            memset(buffer, 0, BUFFER_SIZE);

            int bytes = recv(sock,
                             buffer,
                             BUFFER_SIZE - 1,
                             0);

            if (bytes <= 0)
            {
                printf("\nServer disconnected.\n");
                break;
            }

            buffer[bytes] = '\0';

            printf("%s", buffer);
            fflush(stdout);
        }

        close(sock);
        exit(0);
    }

    /* Parent process */
    else
    {
        while (1)
        {
            if (fgets(buffer,
                      BUFFER_SIZE,
                      stdin) == NULL)
            {
                break;
            }

            send(sock,
                 buffer,
                 strlen(buffer),
                 0);

            if (strncmp(buffer, "/quit", 5) == 0)
            {
                break;
            }
        }

        close(sock);
        wait(NULL);
    }

    return 0;
}
