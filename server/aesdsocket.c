#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/queue.h>
#include <stdbool.h>
#include <time.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

// Structure for thread management
struct thread_data {
    int client_fd;
    pthread_t thread_id;
    bool thread_complete;
    struct sockaddr_in client_addr;
    SLIST_ENTRY(thread_data) entries;
};

// Global Variables
int server_fd = -1;
volatile sig_atomic_t caught_signal = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
SLIST_HEAD(slisthead, thread_data) head;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        caught_signal = 1;
    }
}

void cleanup() {
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE);
    pthread_mutex_destroy(&file_mutex);
    closelog();
}

// Thread to handle individual client connections
void* thread_handler(void* thread_param) {
    struct thread_data* data = (struct thread_data*)thread_param;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &data->client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        data->thread_complete = true;
        return NULL;
    }

    ssize_t bytes_recv;
    size_t total_received = 0;
    
    // Receive data until newline
    while ((bytes_recv = recv(data->client_fd, buffer + total_received, BUFFER_SIZE - 1, 0)) > 0) {
        total_received += bytes_recv;
        if (memchr(buffer, '\n', total_received)) break;
        
        // Dynamic realloc if buffer is full (simplified for this assignment)
        char *new_buf = realloc(buffer, total_received + BUFFER_SIZE);
        if (!new_buf) break;
        buffer = new_buf;
    }

    // Synchronized file write and read-back
    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(DATA_FILE, "a+");
    if (fp) {
        fwrite(buffer, 1, total_received, fp);
        fflush(fp);
        fseek(fp, 0, SEEK_SET);

        char read_buf[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(read_buf, 1, BUFFER_SIZE, fp)) > 0) {
            send(data->client_fd, read_buf, bytes_read, 0);
        }
        fclose(fp);
    }
    pthread_mutex_unlock(&file_mutex);

    free(buffer);
    close(data->client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    data->thread_complete = true;
    return thread_param;
}

// Thread to append timestamp every 10 seconds
void* timestamp_handler(void* arg) {
    while (!caught_signal) {
        // Sleep for 10s, but check for exit signal every second
        for (int i = 0; i < 10 && !caught_signal; i++) {
            sleep(1);
        }
        if (caught_signal) break;

        time_t rawtime;
        struct tm *info;
        char time_buf[128];
        time(&rawtime);
        info = localtime(&rawtime);
        
        strftime(time_buf, sizeof(time_buf), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", info);

        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(DATA_FILE, "a");
        if (fp) {
            fputs(time_buf, fp);
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    SLIST_INIT(&head);

    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (daemon_mode) {
        if (fork() > 0) exit(0);
        setsid();
        chdir("/");
        close(0); close(1); close(2);
    }

    if (listen(server_fd, 10) < 0) {
        cleanup();
        return -1;
    }

    pthread_t time_thread;
    pthread_create(&time_thread, NULL, timestamp_handler, NULL);

    while (!caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        
        if (caught_signal) {
            if (client_fd != -1) close(client_fd);
            break;
        }
        if (client_fd == -1) continue;

        struct thread_data *new_node = malloc(sizeof(struct thread_data));
        new_node->client_fd = client_fd;
        new_node->client_addr = client_addr;
        new_node->thread_complete = false;

        pthread_create(&(new_node->thread_id), NULL, thread_handler, new_node);
        SLIST_INSERT_HEAD(&head, new_node, entries);

        // Housekeeping: Join finished threads
        struct thread_data *it;
        struct thread_data *tmp_node;
        it = SLIST_FIRST(&head);
        while (it != NULL) {
            tmp_node = SLIST_NEXT(it, entries); // Save next before potential free
            if (it->thread_complete) {
                pthread_join(it->thread_id, NULL);
                SLIST_REMOVE(&head, it, thread_data, entries);
                free(it);
            }
            it = tmp_node;
        }
    }

    // Final Cleanup
    syslog(LOG_INFO, "Shutting down...");
    pthread_join(time_thread, NULL);
    struct thread_data *it;
    while (!SLIST_EMPTY(&head)) {
        it = SLIST_FIRST(&head);
        pthread_join(it->thread_id, NULL);
        SLIST_REMOVE_HEAD(&head, entries);
        free(it);
    }

    cleanup();
    return 0;
}