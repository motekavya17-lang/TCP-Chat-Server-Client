/* server.c - Multi-client TCP chat server using select() I/O multiplexing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define PORT_DEFAULT 5000
#define MAX_CLIENTS  FD_SETSIZE
#define NAME_LEN     32
#define BUF_LEN      1024

typedef struct {
    int  fd;                 /* -1 = free slot */
    char name[NAME_LEN];     /* empty until the user registers */
    char buf[BUF_LEN];       /* partial-line buffer */
    int  len;
    char ip[INET_ADDRSTRLEN];
} Client;

static Client clients[MAX_CLIENTS];

static void send_str(int fd, const char *s) { send(fd, s, strlen(s), MSG_NOSIGNAL); }

static void broadcast(const char *msg, int except_fd) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd != -1 && clients[i].name[0] && clients[i].fd != except_fd)
            send_str(clients[i].fd, msg);
}

static int name_taken(const char *n) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd != -1 && strcmp(clients[i].name, n) == 0) return 1;
    return 0;
}

static void drop_client(Client *c, fd_set *master) {
    char msg[128];
    if (c->name[0]) {
        snprintf(msg, sizeof msg, "*** %s left the chat ***\n", c->name);
        printf("[-] %s (%s) disconnected\n", c->name, c->ip);
        close(c->fd); FD_CLR(c->fd, master); c->fd = -1;
        broadcast(msg, -1);
    } else { close(c->fd); FD_CLR(c->fd, master); c->fd = -1; }
    c->name[0] = '\0'; c->len = 0;
}

static void handle_line(Client *c, char *line, fd_set *master) {
    char out[BUF_LEN + NAME_LEN + 16];
    if (!c->name[0]) {                              /* registration step */
        if (line[0] == '\0' || strlen(line) >= NAME_LEN || strchr(line, ' ')) {
            send_str(c->fd, "Invalid name (no spaces, max 31 chars). Try again: "); return;
        }
        if (name_taken(line)) { send_str(c->fd, "Name taken. Try another: "); return; }
        strcpy(c->name, line);
        printf("[+] %s joined from %s\n", c->name, c->ip);
        send_str(c->fd, "Welcome! Commands: /list, /msg <user> <text>, /quit\n");
        snprintf(out, sizeof out, "*** %s joined the chat ***\n", c->name);
        broadcast(out, c->fd);
    } else if (strcmp(line, "/quit") == 0) {
        drop_client(c, master);
    } else if (strcmp(line, "/list") == 0) {
        send_str(c->fd, "Online:");
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].fd != -1 && clients[i].name[0]) {
                send_str(c->fd, " "); send_str(c->fd, clients[i].name);
            }
        send_str(c->fd, "\n");
    } else if (strncmp(line, "/msg ", 5) == 0) {     /* private message */
        char *to = line + 5, *text = strchr(to, ' ');
        if (!text) { send_str(c->fd, "Usage: /msg <user> <text>\n"); return; }
        *text++ = '\0';
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (clients[i].fd != -1 && strcmp(clients[i].name, to) == 0) {
                snprintf(out, sizeof out, "(private) [%s]: %s\n", c->name, text);
                send_str(clients[i].fd, out); return;
            }
        send_str(c->fd, "No such user.\n");
    } else if (line[0]) {                            /* normal broadcast */
        snprintf(out, sizeof out, "[%s]: %s\n", c->name, line);
        printf("%s", out);
        broadcast(out, c->fd);
    }
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : PORT_DEFAULT;
    setvbuf(stdout, NULL, _IOLBF, 0);   /* line-buffered logs */
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port),
                                .sin_addr.s_addr = INADDR_ANY };
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(lfd, 16) < 0) { perror("listen"); return 1; }
    printf("Chat server listening on port %d\n", port);

    fd_set master; FD_ZERO(&master); FD_SET(lfd, &master);
    int maxfd = lfd;

    for (;;) {
        fd_set rd = master;
        if (select(maxfd + 1, &rd, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }
        if (FD_ISSET(lfd, &rd)) {                    /* new connection */
            struct sockaddr_in ca; socklen_t cl = sizeof ca;
            int nfd = accept(lfd, (struct sockaddr *)&ca, &cl);
            if (nfd >= 0 && nfd < MAX_CLIENTS) {
                Client *c = &clients[nfd];
                c->fd = nfd; c->len = 0; c->name[0] = '\0';
                inet_ntop(AF_INET, &ca.sin_addr, c->ip, sizeof c->ip);
                FD_SET(nfd, &master); if (nfd > maxfd) maxfd = nfd;
                send_str(nfd, "Enter username: ");
            } else if (nfd >= 0) close(nfd);
        }
        for (int fd = 0; fd <= maxfd; fd++) {
            if (fd == lfd || !FD_ISSET(fd, &rd) || clients[fd].fd == -1) continue;
            Client *c = &clients[fd];
            int n = recv(fd, c->buf + c->len, BUF_LEN - c->len - 1, 0);
            if (n <= 0) { drop_client(c, &master); continue; }
            c->len += n; c->buf[c->len] = '\0';
            char *nl;                                /* process every full line */
            while (c->fd != -1 && (nl = strchr(c->buf, '\n'))) {
                *nl = '\0';
                if (nl > c->buf && nl[-1] == '\r') nl[-1] = '\0';
                handle_line(c, c->buf, &master);
                if (c->fd == -1) break;
                c->len -= (int)(nl - c->buf) + 1;
                memmove(c->buf, nl + 1, c->len + 1);
            }
            if (c->fd != -1 && c->len >= BUF_LEN - 1) c->len = 0; /* overlong line guard */
        }
    }
    close(lfd);
    return 0;
}
