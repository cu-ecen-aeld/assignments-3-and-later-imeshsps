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
#include <pthread.h>
#ifndef USE_AESD_CHAR_DEVICE
#include <time.h>
#endif

#ifdef USE_AESD_CHAR_DEVICE
#include <sys/ioctl.h>

/* Include ioctl definitions inline to avoid path issues */
struct aesd_seekto {
    uint32_t write_cmd;
    uint32_t write_cmd_offset;
};

#define AESD_IOC_MAGIC 0x16
#define AESDCHAR_IOCSEEKTO _IOWR(AESD_IOC_MAGIC, 1, struct aesd_seekto)
#define AESDCHAR_IOC_MAXNR 1
#endif

#define PORT "9000"
#ifdef USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define TIMESTAMP_INTERVAL 10
#endif
#define BUFFER_SIZE 1024

int sockfd = -1;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile int shutdown_requested = 0;

// Singly linked list node for thread management
struct thread_node {
    pthread_t thread_id;
    int client_fd;
    int completed;
    struct thread_node *next;
};

struct thread_node *thread_list_head = NULL;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifndef USE_AESD_CHAR_DEVICE
// Timer thread data
pthread_t timer_thread;
#endif

// Thread data structure for client connections
struct thread_data {
    int client_fd;
    struct sockaddr_storage client_addr;
};

void cleanup() {
    syslog(LOG_INFO, "Caught signal, exiting");
    
    shutdown_requested = 1;
    
    // Close listening socket to unblock accept()
    if (sockfd != -1) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        sockfd = -1;
    }
    
#ifndef USE_AESD_CHAR_DEVICE
    // Cancel timer thread
    if (timer_thread) {
        pthread_cancel(timer_thread);
        pthread_join(timer_thread, NULL);
    }
#endif
    
    // Wait for all client threads to complete
    pthread_mutex_lock(&thread_list_mutex);
    struct thread_node *current = thread_list_head;
    while (current != NULL) {
        if (!current->completed) {
            // Close client socket to unblock recv()
            close(current->client_fd);
            pthread_join(current->thread_id, NULL);
        }
        struct thread_node *temp = current;
        current = current->next;
        free(temp);
    }
    thread_list_head = NULL;
    pthread_mutex_unlock(&thread_list_mutex);
    
#ifndef USE_AESD_CHAR_DEVICE
    unlink(DATA_FILE);
#endif
    pthread_mutex_destroy(&data_mutex);
    pthread_mutex_destroy(&thread_list_mutex);
    closelog();
}

void signal_handler(int sig __attribute__((unused))) {
    cleanup();
    exit(0);
}

// Add thread to linked list
void add_thread_to_list(pthread_t thread_id, int client_fd) {
    struct thread_node *new_node = malloc(sizeof(struct thread_node));
    new_node->thread_id = thread_id;
    new_node->client_fd = client_fd;
    new_node->completed = 0;
    new_node->next = NULL;
    
    pthread_mutex_lock(&thread_list_mutex);
    if (thread_list_head == NULL) {
        thread_list_head = new_node;
    } else {
        new_node->next = thread_list_head;
        thread_list_head = new_node;
    }
    pthread_mutex_unlock(&thread_list_mutex);
}

// Mark thread as completed
void mark_thread_completed(pthread_t thread_id) {
    pthread_mutex_lock(&thread_list_mutex);
    struct thread_node *current = thread_list_head;
    while (current != NULL) {
        if (pthread_equal(current->thread_id, thread_id)) {
            current->completed = 1;
            break;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&thread_list_mutex);
}

// Clean up completed threads
void cleanup_completed_threads() {
    pthread_mutex_lock(&thread_list_mutex);
    struct thread_node *current = thread_list_head;
    struct thread_node *prev = NULL;
    
    while (current != NULL) {
        if (current->completed) {
            pthread_join(current->thread_id, NULL);
            
            if (prev == NULL) {
                thread_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            
            struct thread_node *temp = current;
            current = current->next;
            free(temp);
        } else {
            prev = current;
            current = current->next;
        }
    }
    pthread_mutex_unlock(&thread_list_mutex);
}

#ifdef USE_AESD_CHAR_DEVICE
// Parse AESDCHAR_IOCSEEKTO command
int parse_seekto_command(const char* buffer, int buffer_len, uint32_t* write_cmd, uint32_t* write_cmd_offset) {
    const char* prefix = "AESDCHAR_IOCSEEKTO:";
    const int prefix_len = strlen(prefix);
    
    // Check if buffer starts with the prefix and ends with newline
    if (buffer_len < prefix_len + 3 || strncmp(buffer, prefix, prefix_len) != 0) {
        return 0; // Not a seek command
    }
    
    // Find the comma separator
    const char* comma = strchr(buffer + prefix_len, ',');
    if (comma == NULL) {
        return 0; // Invalid format
    }
    
    // Find the newline
    const char* newline = strchr(comma, '\n');
    if (newline == NULL) {
        return 0; // Invalid format
    }
    
    // Parse X value (write_cmd)
    char x_str[32];
    int x_len = comma - (buffer + prefix_len);
    if (x_len >= sizeof(x_str)) {
        return 0; // Too long
    }
    strncpy(x_str, buffer + prefix_len, x_len);
    x_str[x_len] = '\0';
    
    // Parse Y value (write_cmd_offset)
    char y_str[32];
    int y_len = newline - (comma + 1);
    if (y_len >= sizeof(y_str)) {
        return 0; // Too long
    }
    strncpy(y_str, comma + 1, y_len);
    y_str[y_len] = '\0';
    
    // Convert to integers
    char* endptr;
    *write_cmd = strtoul(x_str, &endptr, 10);
    if (*endptr != '\0') {
        return 0; // Invalid number
    }
    
    *write_cmd_offset = strtoul(y_str, &endptr, 10);
    if (*endptr != '\0') {
        return 0; // Invalid number
    }
    
    return 1; // Successfully parsed
}
#endif

#ifndef USE_AESD_CHAR_DEVICE
// Timer thread function
void* timer_thread_func(void *arg __attribute__((unused))) {
    struct timespec ts;
    ts.tv_sec = TIMESTAMP_INTERVAL;
    ts.tv_nsec = 0;
    
    while (!shutdown_requested) {
        nanosleep(&ts, NULL);
        
        if (shutdown_requested) {
            break;
        }
        
        // Get current time and format timestamp
        time_t raw_time;
        struct tm *time_info;
        char timestamp[256];
        
        time(&raw_time);
        time_info = localtime(&raw_time);
        
        // RFC 2822 compliant format: "timestamp:Sun, 06 Nov 1994 08:49:37 GMT"
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %Z\n", time_info);
        
        // Write timestamp to file with mutex protection
        pthread_mutex_lock(&data_mutex);
        int fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd != -1) {
            write(fd, timestamp, strlen(timestamp));
            close(fd);
        }
        pthread_mutex_unlock(&data_mutex);
    }
    
    return NULL;
}
#endif

// Client thread function
void* client_thread_func(void *arg) {
    struct thread_data *data = (struct thread_data*)arg;
    int client_fd = data->client_fd;
    struct sockaddr_storage client_addr = data->client_addr;
    free(data);
    
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    
    // Get client IP for logging
    inet_ntop(client_addr.ss_family,
              (client_addr.ss_family == AF_INET) ? 
                  (void *)&(((struct sockaddr_in *)&client_addr)->sin_addr) : 
                  (void *)&(((struct sockaddr_in6 *)&client_addr)->sin6_addr),
              client_ip, sizeof client_ip);
    
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);
    
    while (!shutdown_requested && (bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        pthread_mutex_lock(&data_mutex);
        
#ifdef USE_AESD_CHAR_DEVICE
        // Check if this is a seek command
        uint32_t write_cmd, write_cmd_offset;
        if (parse_seekto_command(buffer, bytes_received, &write_cmd, &write_cmd_offset)) {
            // This is a seek command - handle it specially
            int data_fd = open(DATA_FILE, O_RDWR);
            if (data_fd == -1) {
                syslog(LOG_ERR, "Failed to open data file for seek: %s", strerror(errno));
                pthread_mutex_unlock(&data_mutex);
                close(client_fd);
                mark_thread_completed(pthread_self());
                return NULL;
            }
            
            // Perform the ioctl seek operation
            struct aesd_seekto seekto;
            seekto.write_cmd = write_cmd;
            seekto.write_cmd_offset = write_cmd_offset;
            
            if (ioctl(data_fd, AESDCHAR_IOCSEEKTO, &seekto) == -1) {
                syslog(LOG_ERR, "IOCTL seek failed: %s", strerror(errno));
                close(data_fd);
                pthread_mutex_unlock(&data_mutex);
                close(client_fd);
                mark_thread_completed(pthread_self());
                return NULL;
            }
            
            // Read from current position and send back to client
            char read_buffer[BUFFER_SIZE];
            ssize_t read_bytes;
            while ((read_bytes = read(data_fd, read_buffer, BUFFER_SIZE)) > 0) {
                if (send(client_fd, read_buffer, read_bytes, 0) == -1) {
                    syslog(LOG_ERR, "Send failed after seek: %s", strerror(errno));
                    close(data_fd);
                    pthread_mutex_unlock(&data_mutex);
                    close(client_fd);
                    mark_thread_completed(pthread_self());
                    return NULL;
                }
            }
            
            close(data_fd);
            pthread_mutex_unlock(&data_mutex);
            continue; // Don't process this as a regular write
        }
        
        // Regular write processing for non-seek commands
        // Open device file for each operation to avoid blocking
        int data_fd = open(DATA_FILE, O_RDWR);
#else
        int data_fd = open(DATA_FILE, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
        if (data_fd == -1) {
            syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
            pthread_mutex_unlock(&data_mutex);
            close(client_fd);
            mark_thread_completed(pthread_self());
            return NULL;
        }
        
        // Write all received bytes and check for newlines
        for (int i = 0; i < bytes_received; i++) {
            if (write(data_fd, &buffer[i], 1) != 1) {
                syslog(LOG_ERR, "Write failed");
                close(data_fd);
                pthread_mutex_unlock(&data_mutex);
                close(client_fd);
                mark_thread_completed(pthread_self());
                return NULL;
            }
            
            // If newline, send entire file content back to client
            if (buffer[i] == '\n') {
#ifdef USE_AESD_CHAR_DEVICE
                // Close and reopen for reading from the beginning
                close(data_fd);
                data_fd = open(DATA_FILE, O_RDONLY);
                if (data_fd == -1) {
                    syslog(LOG_ERR, "Failed to reopen data file for reading");
                    pthread_mutex_unlock(&data_mutex);
                    close(client_fd);
                    mark_thread_completed(pthread_self());
                    return NULL;
                }
#else
                lseek(data_fd, 0, SEEK_SET);
#endif
                char read_buffer[BUFFER_SIZE];
                ssize_t read_bytes;
                while ((read_bytes = read(data_fd, read_buffer, BUFFER_SIZE)) > 0) {
                    if (send(client_fd, read_buffer, read_bytes, 0) == -1) {
                        syslog(LOG_ERR, "Send failed");
                        close(data_fd);
                        pthread_mutex_unlock(&data_mutex);
                        close(client_fd);
                        mark_thread_completed(pthread_self());
                        return NULL;
                    }
                }
#ifndef USE_AESD_CHAR_DEVICE
                lseek(data_fd, 0, SEEK_END);
#endif
            }
        }
        
        close(data_fd);
        pthread_mutex_unlock(&data_mutex);
    }
    
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    
    // Mark this thread as completed
    mark_thread_completed(pthread_self());
    
    return NULL;
}

int main(int argc, char *argv[]) {
    struct addrinfo hints, *res, *p;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_size;
    int client_fd;
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
        
        // Set socket options to reuse address
        int yes = 1;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            syslog(LOG_ERR, "setsockopt failed");
            close(sockfd);
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

#ifndef USE_AESD_CHAR_DEVICE
    // Start timer thread only when not using char device
    if (pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timer thread");
        close(sockfd);
        return -1;
    }
#endif

    while (!shutdown_requested) {
        client_addr_size = sizeof client_addr;
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_size);
        
        if (client_fd == -1) {
            if (errno == EINTR) {
                break;
            }
            syslog(LOG_ERR, "Accept failed");
            continue;
        }

        // Create thread data
        struct thread_data *data = malloc(sizeof(struct thread_data));
        data->client_fd = client_fd;
        data->client_addr = client_addr;

        // Create new thread for client
        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_thread_func, data) != 0) {
            syslog(LOG_ERR, "Failed to create client thread");
            close(client_fd);
            free(data);
            continue;
        }

        // Add thread to management list
        add_thread_to_list(client_thread, client_fd);
        
        // Periodically clean up completed threads
        cleanup_completed_threads();
    }

    cleanup();
    return 0;
}