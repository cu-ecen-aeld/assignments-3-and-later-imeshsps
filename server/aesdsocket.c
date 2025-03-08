#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT "9000"
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int sockfd;
int data_fd;

void cleanup() {
    syslog(LOG_INFO, "Caught signal, exiting");
    if (sockfd != -1) {
        close(sockfd);
    }
    if (data_fd != -1) {
        close(data_fd);
    }
    unlink(DATA_FILE);
    closelog();
}

void signal_handler(int sig __attribute__((unused))) {
    cleanup();
    exit(0);
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_size;
    int client_fd;
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int daemon_mode = 0;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed");
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(sockfd);
    }

    freeaddrinfo(res);

    if (p == NULL) {
        syslog(LOG_ERR, "Failed to bind");
        return -1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            close(sockfd);
            return -1;
        }
        if (pid > 0) {
            close(sockfd);
            return 0; // Parent exits
        }
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    if (listen(sockfd, 5) == -1) {
        syslog(LOG_ERR, "Listen failed");
        close(sockfd);
        return -1;
    }

    while (1) {
        client_addr_size = sizeof client_addr;
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_size);
        if (client_fd == -1) {
            if(errno == EINTR) {
                break;
            }
            syslog(LOG_ERR, "Accept failed");
            continue;
        }

        inet_ntop(client_addr.ss_family,
                  (client_addr.ss_family == AF_INET) ? (void *)&(((struct sockaddr_in *)&client_addr)->sin_addr) : (void *)&(((struct sockaddr_in6 *)&client_addr)->sin6_addr),
                  client_ip, sizeof client_ip);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (data_fd == -1) {
            syslog(LOG_ERR, "Failed to open data file");
            close(client_fd);
            continue;
        }

        while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            for (int i = 0; i < bytes_received; i++) {
                write(data_fd, &buffer[i], 1);
                if (buffer[i] == '\n') {
                    lseek(data_fd, 0, SEEK_SET);
                    char read_buffer[BUFFER_SIZE];
                    ssize_t read_bytes;
                    while ((read_bytes = read(data_fd, read_buffer, BUFFER_SIZE)) > 0) {
                        send(client_fd, read_buffer, read_bytes, 0);
                    }
                    lseek(data_fd, 0, SEEK_END);
                }
            }
        }

        close(data_fd);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    cleanup();
    return 0;
}